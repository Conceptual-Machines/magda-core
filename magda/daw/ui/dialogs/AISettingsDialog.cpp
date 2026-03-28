#include "AISettingsDialog.hpp"

#include <juce_llm/juce_llm.h>

#include "../../../agents/llama_model_manager.hpp"
#include "../../../agents/llm_config_utils.hpp"
#include "../../../agents/llm_presets.hpp"
#include "../../../agents/model_downloader.hpp"
#include "../../core/Config.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"

namespace magda {

// ============================================================================
// Helpers
// ============================================================================

namespace {

void styleLabel(juce::Label& label, float size = 12.0f) {
    label.setFont(FontManager::getInstance().getUIFont(size));
    label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
}

void styleEditor(juce::TextEditor& ed, const juce::String& placeholder, bool password = false) {
    ed.setFont(FontManager::getInstance().getUIFont(12.0f));
    ed.setTextToShowWhenEmpty(placeholder, DarkTheme::getColour(DarkTheme::TEXT_DIM));
    ed.setColour(juce::TextEditor::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    ed.setColour(juce::TextEditor::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    ed.setColour(juce::TextEditor::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    if (password)
        ed.setPasswordCharacter(static_cast<juce::juce_wchar>('*'));
}

void styleCombo(juce::ComboBox& combo) {
    combo.setColour(juce::ComboBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    combo.setColour(juce::ComboBox::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
}

// Known provider types for the "Add Provider" picker
struct ProviderInfo {
    const char* id;  // "openai_chat", "anthropic", "gemini", "deepseek", "openrouter", "local"
    const char* displayName;  // "OpenAI", "Anthropic", etc.
    bool isLocal;             // true for local llama-server
};

const std::vector<ProviderInfo>& getKnownProviders() {
    static const std::vector<ProviderInfo> providers = {
        {"openai_chat", "OpenAI", false},    {"anthropic", "Anthropic", false},
        {"gemini", "Gemini", false},         {"deepseek", "DeepSeek", false},
        {"openrouter", "OpenRouter", false}, {"local", "Local (llama-server)", true},
    };
    return providers;
}

const ProviderInfo* findProviderInfo(const std::string& id) {
    for (const auto& p : getKnownProviders())
        if (p.id == id)
            return &p;
    return nullptr;
}

}  // namespace

// ============================================================================
// ProviderRow — one credential entry (cloud API key or local URL)
// ============================================================================

class ProviderRow : public juce::Component {
  public:
    std::function<void()> onRemove;
    std::function<void()> onChanged;

    ProviderRow(const std::string& providerId, bool isLocal)
        : providerId_(providerId), isLocal_(isLocal) {
        auto* info = findProviderInfo(providerId);
        juce::String name = info ? info->displayName : juce::String(providerId);

        nameLabel_.setText(name, juce::dontSendNotification);
        styleLabel(nameLabel_, 13.0f);
        nameLabel_.setFont(nameLabel_.getFont().boldened());
        addAndMakeVisible(nameLabel_);

        if (isLocal_) {
            styleEditor(valueEditor_, "http://127.0.0.1:8080/v1");
        } else {
            styleEditor(valueEditor_, "API key...", true);
        }
        addAndMakeVisible(valueEditor_);

        validateButton_.setButtonText("Test");
        validateButton_.onClick = [this]() { validate(); };
        addAndMakeVisible(validateButton_);

        styleLabel(statusLabel_, 11.0f);
        statusLabel_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_DIM));
        addAndMakeVisible(statusLabel_);

        removeButton_.setButtonText(juce::String::charToString(0x2715));  // ✕
        removeButton_.onClick = [this]() {
            if (onRemove)
                onRemove();
        };
        addAndMakeVisible(removeButton_);
    }

    void resized() override {
        auto bounds = getLocalBounds();

        auto topRow = bounds.removeFromTop(24);
        nameLabel_.setBounds(topRow.removeFromLeft(160));
        removeButton_.setBounds(topRow.removeFromRight(28).reduced(2));

        bounds.removeFromTop(2);
        auto editorRow = bounds.removeFromTop(26);
        validateButton_.setBounds(editorRow.removeFromRight(60).reduced(2, 1));
        editorRow.removeFromRight(4);
        valueEditor_.setBounds(editorRow.reduced(0, 1));

        bounds.removeFromTop(2);
        statusLabel_.setBounds(bounds.removeFromTop(18));
    }

    static constexpr int kRowHeight = 76;

    std::string getProviderId() const {
        return providerId_;
    }
    bool isLocal() const {
        return isLocal_;
    }

    juce::String getValue() const {
        return valueEditor_.getText();
    }
    void setValue(const juce::String& v) {
        valueEditor_.setText(v, juce::dontSendNotification);
    }

  private:
    void validate() {
        validateButton_.setEnabled(false);
        statusLabel_.setText("Testing...", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

        auto value = valueEditor_.getText();
        auto providerId = providerId_;
        auto isLocal = isLocal_;
        auto safeThis = juce::Component::SafePointer<ProviderRow>(this);

        juce::Thread::launch([safeThis, value, providerId, isLocal]() {
            juce::String result;
            bool ok = false;

            if (isLocal) {
                // Check local server with short timeout
                Config::AgentLLMConfig cfg;
                cfg.provider = "openai_chat";
                cfg.baseUrl = value.toStdString();
                cfg.model = "local";
                auto pc = toLLMProviderConfig(cfg);
                pc.apiKey = "no-key";           // local doesn't need one
                pc.connectionTimeoutMs = 5000;  // 5s timeout for local server
                auto client = llm::LLMClientFactory::create(pc);
                llm::Request req;
                req.systemPrompt = "Reply OK.";
                req.userMessage = "ping";
                req.temperature = 0.0f;
                auto resp = client->sendRequest(req);
                if (resp.success) {
                    ok = true;
                    result = "Connected (" + juce::String(resp.wallSeconds, 1) + "s)";
                } else if (resp.error.contains("connect")) {
                    result = "Server not reachable";
                } else {
                    result = resp.error.substring(0, 50);
                }
            } else {
                // Test cloud provider with API key
                Config::AgentLLMConfig cfg;
                cfg.provider = providerId;
                cfg.apiKey = value.toStdString();

                // Use a cheap model for testing
                if (providerId == "openai_chat")
                    cfg.model = "gpt-4.1-mini";
                else if (providerId == "anthropic")
                    cfg.model = "claude-haiku-4-5-20251001";
                else if (providerId == "gemini")
                    cfg.model = "gemini-2.0-flash";
                else if (providerId == "deepseek") {
                    cfg.provider = "openai_chat";
                    cfg.baseUrl = "https://api.deepseek.com";
                    cfg.model = "deepseek-chat";
                } else if (providerId == "openrouter") {
                    cfg.provider = "openai_chat";
                    cfg.baseUrl = "https://openrouter.ai/api/v1";
                    cfg.model = "meta-llama/llama-3.3-70b-instruct";
                }

                auto pc = toLLMProviderConfig(cfg);
                if (pc.apiKey.isEmpty()) {
                    result = "No API key";
                } else {
                    auto client = llm::LLMClientFactory::create(pc);
                    llm::Request req;
                    req.systemPrompt = "Reply OK.";
                    req.userMessage = "ping";
                    req.temperature = 0.0f;
                    auto resp = client->sendRequest(req);
                    if (resp.success) {
                        ok = true;
                        result = "OK (" + juce::String(resp.wallSeconds, 1) + "s)";
                    } else {
                        result = resp.error.substring(0, 50);
                    }
                }
            }

            juce::MessageManager::callAsync([safeThis, ok, result]() {
                if (!safeThis)
                    return;
                safeThis->validateButton_.setEnabled(true);
                safeThis->statusLabel_.setText(result, juce::dontSendNotification);
                safeThis->statusLabel_.setColour(juce::Label::textColourId,
                                                 ok ? juce::Colours::limegreen
                                                    : juce::Colours::orange);
            });
        });
    }

    std::string providerId_;
    bool isLocal_;
    juce::Label nameLabel_;
    juce::TextEditor valueEditor_;
    juce::TextButton validateButton_;
    juce::Label statusLabel_;
    juce::TextButton removeButton_;
};

// ============================================================================
// LocalModelPanel — embedded model loader, replaces old server panel
// ============================================================================

class LocalModelPanel : public juce::Component {
  public:
    LocalModelPanel() {
        titleLabel_.setText("Local Model (Embedded)", juce::dontSendNotification);
        styleLabel(titleLabel_, 13.0f);
        titleLabel_.setFont(titleLabel_.getFont().boldened());
        addAndMakeVisible(titleLabel_);

        // Download model button
        downloadButton_.setButtonText("Download Model");
        downloadButton_.onClick = [this]() { startDownload(); };
        addAndMakeVisible(downloadButton_);

        // Model file
        modelLabel_.setText("Model (.gguf)", juce::dontSendNotification);
        styleLabel(modelLabel_);
        addAndMakeVisible(modelLabel_);

        styleEditor(modelEditor_, "/path/to/model.gguf");
        addAndMakeVisible(modelEditor_);

        browseButton_.setButtonText("...");
        browseButton_.onClick = [this]() { browseModel(); };
        addAndMakeVisible(browseButton_);

        // GPU layers
        gpuLabel_.setText("GPU Layers", juce::dontSendNotification);
        styleLabel(gpuLabel_);
        addAndMakeVisible(gpuLabel_);

        styleEditor(gpuEditor_, "-1 (auto)");
        gpuEditor_.setInputRestrictions(4, "-0123456789");
        addAndMakeVisible(gpuEditor_);

        // Context size
        ctxLabel_.setText("Context", juce::dontSendNotification);
        styleLabel(ctxLabel_);
        addAndMakeVisible(ctxLabel_);

        styleEditor(ctxEditor_, "4096");
        ctxEditor_.setInputRestrictions(6, "0123456789");
        addAndMakeVisible(ctxEditor_);

        // Load/Unload button
        loadButton_.setButtonText("Load Model");
        loadButton_.onClick = [this]() { toggleModel(); };
        addAndMakeVisible(loadButton_);

        // Status
        styleLabel(statusLabel_, 11.0f);
        statusLabel_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_DIM));
        addAndMakeVisible(statusLabel_);

        updateStatus();
    }

    static constexpr int kPanelHeight = 190;

    void resized() override {
        auto bounds = getLocalBounds();
        const int labelW = 90;
        const int rowH = 26;

        titleLabel_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(4);

        // Model path row
        auto row = bounds.removeFromTop(rowH);
        modelLabel_.setBounds(row.removeFromLeft(labelW));
        browseButton_.setBounds(row.removeFromRight(32).reduced(2, 1));
        row.removeFromRight(2);
        modelEditor_.setBounds(row.reduced(0, 1));
        bounds.removeFromTop(2);

        // GPU + Context on one row
        row = bounds.removeFromTop(rowH);
        gpuLabel_.setBounds(row.removeFromLeft(labelW));
        gpuEditor_.setBounds(row.removeFromLeft(50).reduced(0, 1));
        row.removeFromLeft(12);
        ctxLabel_.setBounds(row.removeFromLeft(56));
        ctxEditor_.setBounds(row.removeFromLeft(60).reduced(0, 1));
        bounds.removeFromTop(6);

        // Load/Unload + Download + Status
        row = bounds.removeFromTop(rowH);
        loadButton_.setBounds(row.removeFromLeft(100).reduced(0, 1));
        row.removeFromLeft(8);
        downloadButton_.setBounds(row.removeFromLeft(120).reduced(0, 1));
        row.removeFromLeft(8);
        statusLabel_.setBounds(row);
    }

    void load(const Config& config) {
        modelEditor_.setText(juce::String(config.getLocalModelPath()), juce::dontSendNotification);
        gpuEditor_.setText(juce::String(config.getLocalLlamaGpuLayers()),
                           juce::dontSendNotification);
        ctxEditor_.setText(juce::String(config.getLocalLlamaContextSize()),
                           juce::dontSendNotification);
        updateStatus();
    }

    void apply(Config& config) const {
        config.setLocalModelPath(modelEditor_.getText().toStdString());
        config.setLocalLlamaGpuLayers(gpuEditor_.getText().getIntValue());
        config.setLocalLlamaContextSize(ctxEditor_.getText().getIntValue());
    }

  private:
    void browseModel() {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Select GGUF Model", juce::File(modelEditor_.getText()), "*.gguf");
        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result.existsAsFile())
                    modelEditor_.setText(result.getFullPathName(), juce::dontSendNotification);
            });
    }

    void startDownload() {
        // Ask user where to save the model
        chooser_ =
            std::make_unique<juce::FileChooser>("Save MAGDA Model",
                                                juce::File(modelEditor_.getText())
                                                    .getParentDirectory()
                                                    .getChildFile("magda-v0.3.0-q4_k_m.gguf"),
                                                "*.gguf");
        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                auto result = fc.getResult();
                if (result == juce::File())
                    return;

                auto targetFile = result.withFileExtension("gguf");

                downloadButton_.setEnabled(false);
                downloadButton_.setButtonText("0%");
                statusLabel_.setText("Downloading...", juce::dontSendNotification);
                statusLabel_.setColour(juce::Label::textColourId, juce::Colours::yellow);

                downloader_ = std::make_unique<ModelDownloader>();
                downloader_->startDownload(
                    ModelDownloader::getDefaultModelUrl(), targetFile,
                    [this](int64_t bytesDownloaded, int64_t totalBytes) {
                        juce::MessageManager::callAsync([this, bytesDownloaded, totalBytes]() {
                            if (totalBytes > 0) {
                                int pct = static_cast<int>(bytesDownloaded * 100 / totalBytes);
                                downloadButton_.setButtonText(juce::String(pct) + "%");
                                auto mb = static_cast<double>(bytesDownloaded) / (1024.0 * 1024.0);
                                auto totalMb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
                                statusLabel_.setText(juce::String(mb, 0) + " / " +
                                                         juce::String(totalMb, 0) + " MB",
                                                     juce::dontSendNotification);
                            } else {
                                auto mb = static_cast<double>(bytesDownloaded) / (1024.0 * 1024.0);
                                statusLabel_.setText(juce::String(mb, 0) + " MB downloaded",
                                                     juce::dontSendNotification);
                            }
                        });
                    },
                    [this](bool success, const juce::String& modelPath) {
                        juce::MessageManager::callAsync([this, success, modelPath]() {
                            downloadButton_.setEnabled(true);
                            downloadButton_.setButtonText("Download Model");

                            if (success) {
                                modelEditor_.setText(modelPath, juce::dontSendNotification);
                                statusLabel_.setText("Download complete",
                                                     juce::dontSendNotification);
                                statusLabel_.setColour(juce::Label::textColourId,
                                                       juce::Colours::limegreen);
                            } else {
                                statusLabel_.setText("Download failed", juce::dontSendNotification);
                                statusLabel_.setColour(juce::Label::textColourId,
                                                       juce::Colours::red);
                            }
                        });
                    });
            });
    }

    void toggleModel() {
        auto& mgr = LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            mgr.unloadModel();
        } else {
            LlamaModelManager::Config cfg;
            cfg.modelPath = modelEditor_.getText().toStdString();
            cfg.gpuLayers = gpuEditor_.getText().getIntValue();
            cfg.contextSize = ctxEditor_.getText().getIntValue();

            loadButton_.setEnabled(false);
            statusLabel_.setText("Loading...", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::yellow);

            // Load in background thread to avoid blocking UI
            std::thread([this, cfg]() {
                bool ok = LlamaModelManager::getInstance().loadModel(cfg);
                juce::MessageManager::callAsync([this, ok, cfg]() {
                    loadButton_.setEnabled(true);
                    if (ok) {
                        // Switch config to use embedded model
                        auto& config = Config::getInstance();
                        config.setLocalModelPath(cfg.modelPath);
                        config.setLocalLlamaGpuLayers(cfg.gpuLayers);
                        config.setLocalLlamaContextSize(cfg.contextSize);
                        config.setAIPreset("local_embedded");
                        auto* preset = magda::findPreset("local_embedded");
                        if (preset) {
                            for (const auto& [role, presetCfg] : preset->agents)
                                config.setAgentLLMConfig(role, presetCfg);
                        }
                        config.save();
                    } else {
                        statusLabel_.setText("Failed to load model", juce::dontSendNotification);
                    }
                    updateStatus();
                });
            }).detach();
            return;
        }
        updateStatus();
    }

    void updateStatus() {
        auto& mgr = LlamaModelManager::getInstance();
        if (mgr.isLoaded()) {
            loadButton_.setButtonText("Unload");
            auto path = juce::File(mgr.getLoadedModelPath()).getFileName();
            statusLabel_.setText("Loaded: " + path, juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        } else {
            loadButton_.setButtonText("Load Model");
            statusLabel_.setText("No model loaded", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_DIM));
        }
    }

    juce::Label titleLabel_, modelLabel_, gpuLabel_, ctxLabel_;
    juce::TextEditor modelEditor_, gpuEditor_, ctxEditor_;
    juce::TextButton browseButton_, downloadButton_, loadButton_;
    juce::Label statusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<ModelDownloader> downloader_;
};

// ============================================================================
// CredentialsPage — cloud credentials + local server panel
// ============================================================================

class AISettingsDialog::CredentialsPage : public juce::Component {
  public:
    CredentialsPage() {
        // "Add Provider" row — cloud providers only
        addProviderCombo_.addItem("Add provider...", 1);
        int itemId = 2;
        for (const auto& p : getKnownProviders()) {
            if (!p.isLocal)
                addProviderCombo_.addItem(p.displayName, itemId);
            ++itemId;
        }
        styleCombo(addProviderCombo_);
        addProviderCombo_.onChange = [this]() {
            int sel = addProviderCombo_.getSelectedId();
            if (sel <= 1)
                return;
            auto idx = static_cast<size_t>(sel - 2);
            const auto& providers = getKnownProviders();
            if (idx < providers.size() && !providers[idx].isLocal) {
                addProvider(providers[idx].id);
                addProviderCombo_.setSelectedId(1, juce::dontSendNotification);
            }
        };
        addAndMakeVisible(addProviderCombo_);

        // Cloud row container
        addAndMakeVisible(rowContainer_);

        // Local server panel (always visible)
        localPanel_ = std::make_unique<LocalModelPanel>();
        addAndMakeVisible(*localPanel_);

        // Separator label
        localSeparator_.setText("Local Model", juce::dontSendNotification);
        styleLabel(localSeparator_, 11.0f);
        localSeparator_.setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_DIM));
        addAndMakeVisible(localSeparator_);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);

        // Cloud provider rows
        int totalRowH = static_cast<int>(rows_.size()) * (ProviderRow::kRowHeight + 4);
        rowContainer_.setBounds(bounds.removeFromTop(totalRowH));

        auto containerBounds = rowContainer_.getLocalBounds();
        for (auto* row : rows_) {
            row->setBounds(containerBounds.removeFromTop(ProviderRow::kRowHeight));
            containerBounds.removeFromTop(4);
        }

        bounds.removeFromTop(4);
        addProviderCombo_.setBounds(bounds.removeFromTop(28).removeFromLeft(200));

        // Separator + local panel
        bounds.removeFromTop(12);
        localSeparator_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);
        localPanel_->setBounds(bounds.removeFromTop(LocalModelPanel::kPanelHeight));
    }

    void load(const Config& config) {
        // Clear existing cloud rows
        rows_.clear();
        ownedRows_.clear();
        rowContainer_.removeAllChildren();

        for (const auto& [provider, key] : config.getAllAICredentials()) {
            if (key.empty())
                continue;
            auto* info = findProviderInfo(provider);
            if (!info || info->isLocal)
                continue;
            auto& row = addProviderInternal(provider);
            row.setValue(juce::String(key));
        }

        localPanel_->load(config);
        updateAddCombo();
        resized();
    }

    void apply(Config& config) const {
        // Clear and rebuild cloud credentials
        for (const auto& provider :
             {"openai_chat", "anthropic", "gemini", "deepseek", "openrouter"})
            config.setAICredential(provider, "");

        for (auto* row : rows_)
            config.setAICredential(row->getProviderId(), row->getValue().toStdString());

        localPanel_->apply(config);
    }

  private:
    ProviderRow& addProviderInternal(const std::string& id) {
        auto row = std::make_unique<ProviderRow>(id, false);
        auto* rawPtr = row.get();
        row->onRemove = [this, rawPtr]() { removeProvider(rawPtr); };
        rowContainer_.addAndMakeVisible(*row);
        rows_.push_back(rawPtr);
        ownedRows_.push_back(std::move(row));
        return *rawPtr;
    }

    void addProvider(const std::string& id) {
        for (auto* r : rows_) {
            if (r->getProviderId() == id)
                return;
        }

        addProviderInternal(id);
        updateAddCombo();

        if (auto* parent = getParentComponent())
            parent->resized();
        resized();
    }

    void removeProvider(ProviderRow* row) {
        rows_.erase(std::remove(rows_.begin(), rows_.end(), row), rows_.end());
        ownedRows_.erase(std::remove_if(ownedRows_.begin(), ownedRows_.end(),
                                        [row](const auto& p) { return p.get() == row; }),
                         ownedRows_.end());
        updateAddCombo();

        if (auto* parent = getParentComponent())
            parent->resized();
        resized();
    }

    void updateAddCombo() {
        addProviderCombo_.clear();
        addProviderCombo_.addItem("Add provider...", 1);

        int itemId = 2;
        for (const auto& p : getKnownProviders()) {
            if (p.isLocal) {
                ++itemId;
                continue;
            }
            bool exists = false;
            for (auto* r : rows_) {
                if (r->getProviderId() == p.id) {
                    exists = true;
                    break;
                }
            }
            if (!exists)
                addProviderCombo_.addItem(p.displayName, itemId);
            ++itemId;
        }
        addProviderCombo_.setSelectedId(1, juce::dontSendNotification);
    }

    juce::Component rowContainer_;
    std::vector<ProviderRow*> rows_;
    std::vector<std::unique_ptr<ProviderRow>> ownedRows_;
    juce::ComboBox addProviderCombo_;
    juce::Label localSeparator_;
    std::unique_ptr<LocalModelPanel> localPanel_;
};

// ============================================================================
// ConfigPage — Easy (preset) / Advanced (per-agent mapping)
// ============================================================================

class AISettingsDialog::ConfigPage : public juce::Component {
  public:
    ConfigPage() {
        easyButton_.setRadioGroupId(1);
        advancedButton_.setRadioGroupId(1);
        easyButton_.setClickingTogglesState(true);
        advancedButton_.setClickingTogglesState(true);
        easyButton_.setToggleState(true, juce::dontSendNotification);

        easyButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        advancedButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::black);

        easyButton_.onClick = [this]() { setMode(true); };
        advancedButton_.onClick = [this]() { setMode(false); };
        addAndMakeVisible(easyButton_);
        addAndMakeVisible(advancedButton_);

        // Easy mode: preset combo
        int itemId = 1;
        for (const auto& preset : magda::getBuiltInPresets())
            presetCombo_.addItem(juce::String(preset.displayName), itemId++);
        styleCombo(presetCombo_);
        addAndMakeVisible(presetCombo_);

        presetLabel_.setText("Preset", juce::dontSendNotification);
        styleLabel(presetLabel_);
        addAndMakeVisible(presetLabel_);

        // Advanced mode: per-agent rows
        const char* roles[] = {"Router", "Command", "Music"};
        for (int i = 0; i < 3; ++i)
            agentRows_[static_cast<size_t>(i)].init(*this, roles[i]);

        setMode(true);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int rowH = 28;

        // Mode toggle
        auto toggleRow = bounds.removeFromTop(rowH);
        easyButton_.setBounds(toggleRow.removeFromLeft(80).reduced(0, 2));
        toggleRow.removeFromLeft(4);
        advancedButton_.setBounds(toggleRow.removeFromLeft(100).reduced(0, 2));
        bounds.removeFromTop(12);

        if (easyMode_) {
            auto row = bounds.removeFromTop(rowH);
            presetLabel_.setBounds(row.removeFromLeft(80));
            presetCombo_.setBounds(row.reduced(0, 2));
        } else {
            for (int i = 0; i < 3; ++i) {
                agentRows_[static_cast<size_t>(i)].layout(bounds, rowH);
                bounds.removeFromTop(8);
            }
        }
    }

    void load(const Config& config) {
        auto presetId = config.getAIPreset();
        auto* preset = magda::findPreset(presetId);

        if (preset) {
            easyMode_ = true;
            easyButton_.setToggleState(true, juce::dontSendNotification);

            const auto& presets = magda::getBuiltInPresets();
            for (size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].id == presetId) {
                    presetCombo_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
                    break;
                }
            }
        } else {
            easyMode_ = false;
            advancedButton_.setToggleState(true, juce::dontSendNotification);
        }

        const std::string roles[] = {"router", "command", "music"};
        for (int i = 0; i < 3; ++i)
            agentRows_[static_cast<size_t>(i)].loadFrom(config.getAgentLLMConfig(roles[i]));

        setMode(easyMode_);
    }

    void apply(Config& config) const {
        if (easyMode_) {
            auto presetId = getSelectedPresetId();
            config.setAIPreset(presetId);

            auto* preset = magda::findPreset(presetId);
            if (preset) {
                for (const auto& [role, presetCfg] : preset->agents) {
                    auto cfg = presetCfg;
                    cfg.apiKey = "";  // credentials page handles keys
                    config.setAgentLLMConfig(role, cfg);
                }
            }
        } else {
            config.setAIPreset("custom");
            const std::string roles[] = {"router", "command", "music"};
            for (int i = 0; i < 3; ++i)
                config.setAgentLLMConfig(roles[i],
                                         agentRows_[static_cast<size_t>(i)].toConfig(roles[i]));
        }
    }

  private:
    struct AgentRow {
        juce::Label nameLabel;
        juce::Label providerLabel;
        juce::ComboBox providerCombo;

        void init(juce::Component& parent, const char* name) {
            nameLabel.setText(name, juce::dontSendNotification);
            styleLabel(nameLabel, 13.0f);
            nameLabel.setFont(nameLabel.getFont().boldened());
            parent.addAndMakeVisible(nameLabel);

            providerLabel.setText("Provider", juce::dontSendNotification);
            styleLabel(providerLabel);
            parent.addAndMakeVisible(providerLabel);

            providerCombo.addItem("OpenAI", 1);
            providerCombo.addItem("Anthropic", 2);
            providerCombo.addItem("Gemini", 3);
            providerCombo.addItem("Local (Embedded)", 4);
            styleCombo(providerCombo);
            parent.addAndMakeVisible(providerCombo);
        }

        void layout(juce::Rectangle<int>& bounds, int rowH) {
            const int labelW = 80;
            nameLabel.setBounds(bounds.removeFromTop(22));
            bounds.removeFromTop(2);

            auto row = bounds.removeFromTop(rowH);
            providerLabel.setBounds(row.removeFromLeft(labelW));
            providerCombo.setBounds(row.reduced(0, 2));
        }

        void loadFrom(const Config::AgentLLMConfig& cfg) {
            if (cfg.provider == "anthropic")
                providerCombo.setSelectedId(2, juce::dontSendNotification);
            else if (cfg.provider == "gemini")
                providerCombo.setSelectedId(3, juce::dontSendNotification);
            else if (cfg.provider == "llama_local")
                providerCombo.setSelectedId(4, juce::dontSendNotification);
            else
                providerCombo.setSelectedId(1, juce::dontSendNotification);
        }

        Config::AgentLLMConfig toConfig(const std::string& role) const {
            Config::AgentLLMConfig cfg;
            switch (providerCombo.getSelectedId()) {
                case 2:
                    cfg.provider = "anthropic";
                    cfg.model =
                        (role == "router") ? "claude-haiku-4-5-20251001" : "claude-opus-4-6";
                    break;
                case 3:
                    cfg.provider = "gemini";
                    cfg.model = (role == "router") ? "gemini-2.0-flash" : "gemini-2.5-pro";
                    break;
                case 4:
                    cfg.provider = "llama_local";
                    break;
                default:
                    cfg.provider = "openai_chat";
                    cfg.model = (role == "router") ? "gpt-4.1" : "gpt-5";
                    break;
            }
            cfg.apiKey = "";
            return cfg;
        }

        void setVisible(bool visible) {
            nameLabel.setVisible(visible);
            providerLabel.setVisible(visible);
            providerCombo.setVisible(visible);
        }
    };

    void setMode(bool easy) {
        easyMode_ = easy;
        presetCombo_.setVisible(easy);
        presetLabel_.setVisible(easy);
        for (auto& row : agentRows_)
            row.setVisible(!easy);
        resized();
    }

    std::string getSelectedPresetId() const {
        int idx = presetCombo_.getSelectedId() - 1;
        const auto& presets = magda::getBuiltInPresets();
        if (idx >= 0 && idx < static_cast<int>(presets.size()))
            return presets[static_cast<size_t>(idx)].id;
        return "cloud_openai";
    }

    bool easyMode_ = true;
    juce::TextButton easyButton_{"Easy"};
    juce::TextButton advancedButton_{"Advanced"};
    juce::Label presetLabel_;
    juce::ComboBox presetCombo_;
    std::array<AgentRow, 3> agentRows_;
};

// ============================================================================
// AISettingsDialog
// ============================================================================

AISettingsDialog::AISettingsDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());
    credentialsPage_ = std::make_unique<CredentialsPage>();
    configPage_ = std::make_unique<ConfigPage>();

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabbedComponent_.addTab("Credentials", tabBg, credentialsPage_.get(), false);
    tabbedComponent_.addTab("Configuration", tabBg, configPage_.get(), false);
    addAndMakeVisible(tabbedComponent_);

    okButton_.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible(okButton_);

    cancelButton_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible(cancelButton_);

    loadSettings();

    setSize(540, 600);
}

AISettingsDialog::~AISettingsDialog() {
    setLookAndFeel(nullptr);
}

void AISettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void AISettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(8);

    // Bottom buttons
    auto buttonRow = bounds.removeFromBottom(36);
    cancelButton_.setBounds(buttonRow.removeFromRight(80).reduced(0, 4));
    buttonRow.removeFromRight(8);
    okButton_.setBounds(buttonRow.removeFromRight(80).reduced(0, 4));
    bounds.removeFromBottom(4);

    // Tabs get the rest
    tabbedComponent_.setBounds(bounds);
}

void AISettingsDialog::loadSettings() {
    auto& config = Config::getInstance();
    credentialsPage_->load(config);
    configPage_->load(config);
}

void AISettingsDialog::applySettings() {
    auto& config = Config::getInstance();
    credentialsPage_->apply(config);
    configPage_->apply(config);
    config.save();
}

void AISettingsDialog::showDialog(juce::Component* parent) {
    (void)parent;
    auto* dialog = new AISettingsDialog();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "AI Settings";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
