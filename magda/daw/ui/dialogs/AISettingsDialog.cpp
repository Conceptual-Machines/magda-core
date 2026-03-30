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

// Known cloud providers
struct ProviderInfo {
    const char* id;           // credential key: "openai_chat", "anthropic", etc.
    const char* displayName;  // "OpenAI", "Anthropic", etc.
    const char* testProvider;
    const char* testBaseUrl;
    const char* testModel;
    const char* iconData;
    int iconDataSize;
};

const std::vector<ProviderInfo>& getKnownProviders() {
    static const std::vector<ProviderInfo> providers = {
        {"openai_chat", "OpenAI", "openai_chat", "", "gpt-4.1-mini", BinaryData::openai_svg,
         BinaryData::openai_svgSize},
        {"anthropic", "Anthropic", "anthropic", "", "claude-haiku-4-5-20251001",
         BinaryData::anthropic_svg, BinaryData::anthropic_svgSize},
        {"gemini", "Gemini", "gemini", "", "gemini-2.0-flash", BinaryData::gemini_svg,
         BinaryData::gemini_svgSize},
        {"deepseek", "DeepSeek", "deepseek", "", "deepseek-chat", BinaryData::deepseek_svg,
         BinaryData::deepseek_svgSize},
        {"openrouter", "OpenRouter", "openrouter", "", "meta-llama/llama-3.3-70b-instruct",
         BinaryData::openrouter_svg, BinaryData::openrouter_svgSize},
    };
    return providers;
}

std::unique_ptr<juce::Drawable> createProviderIcon(const ProviderInfo& info) {
    return juce::Drawable::createFromImageData(info.iconData,
                                               static_cast<size_t>(info.iconDataSize));
}

const ProviderInfo* findProviderInfo(const std::string& id) {
    for (const auto& p : getKnownProviders())
        if (p.id == id)
            return &p;
    return nullptr;
}

}  // namespace

// ============================================================================
// CloudPage — manage cloud provider API keys
//
// Top: provider combo + API key field + Test + Add buttons
// Bottom: compact list of registered providers with remove buttons
// ============================================================================

class AISettingsDialog::CloudPage : public juce::Component {
  public:
    CloudPage() {
        // Provider selector
        providerLabel_.setText("Provider", juce::dontSendNotification);
        styleLabel(providerLabel_);
        addAndMakeVisible(providerLabel_);

        {
            auto* menu = providerCombo_.getRootMenu();
            int itemId = 1;
            for (const auto& p : getKnownProviders()) {
                auto icon = createProviderIcon(p);
                juce::PopupMenu::Item item;
                item.text = p.displayName;
                item.itemID = itemId++;
                item.image = std::move(icon);
                menu->addItem(item);
            }
        }
        providerCombo_.setSelectedId(1, juce::dontSendNotification);
        styleCombo(providerCombo_);
        addAndMakeVisible(providerCombo_);

        // API key input
        keyLabel_.setText("API Key", juce::dontSendNotification);
        styleLabel(keyLabel_);
        addAndMakeVisible(keyLabel_);

        styleEditor(keyEditor_, "Enter API key...", true);
        addAndMakeVisible(keyEditor_);

        // Test button
        testBtn_.setButtonText("Test");
        testBtn_.onClick = [this]() { testKey(); };
        addAndMakeVisible(testBtn_);

        // Status label
        styleLabel(statusLabel_, 11.0f);
        statusLabel_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_DIM));
        addAndMakeVisible(statusLabel_);

        // Add/Save button
        addBtn_.setButtonText("Add");
        addBtn_.onClick = [this]() { addCurrentProvider(); };
        addAndMakeVisible(addBtn_);

        // Registered providers header
        registeredLabel_.setText("Registered Providers", juce::dontSendNotification);
        styleLabel(registeredLabel_, 11.0f);
        registeredLabel_.setColour(juce::Label::textColourId,
                                   DarkTheme::getColour(DarkTheme::TEXT_DIM));
        addAndMakeVisible(registeredLabel_);

        addAndMakeVisible(listContainer_);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int labelW = 70;
        const int rowH = 28;

        // Provider combo row
        auto row = bounds.removeFromTop(rowH);
        providerLabel_.setBounds(row.removeFromLeft(labelW));
        providerCombo_.setBounds(row.reduced(0, 2));
        bounds.removeFromTop(4);

        // API key row
        row = bounds.removeFromTop(rowH);
        keyLabel_.setBounds(row.removeFromLeft(labelW));
        testBtn_.setBounds(row.removeFromRight(50).reduced(2, 2));
        row.removeFromRight(4);
        addBtn_.setBounds(row.removeFromRight(50).reduced(2, 2));
        row.removeFromRight(4);
        keyEditor_.setBounds(row.reduced(0, 1));
        bounds.removeFromTop(2);

        // Status
        statusLabel_.setBounds(bounds.removeFromTop(18).withTrimmedLeft(labelW));
        bounds.removeFromTop(8);

        // Registered providers list
        registeredLabel_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(4);

        int totalH = static_cast<int>(entries_.size()) * kListRowHeight;
        listContainer_.setBounds(bounds.removeFromTop(totalH));

        auto listBounds = listContainer_.getLocalBounds();
        for (auto& entry : entries_) {
            entry.layout(listBounds.removeFromTop(kListRowHeight));
        }
    }

    void paint(juce::Graphics& g) override {
        auto borderColour = DarkTheme::getColour(DarkTheme::BORDER);
        float left = 12.0f;
        float right = static_cast<float>(getWidth() - 12);

        // Separator above registered list
        if (registeredLabel_.isVisible()) {
            g.setColour(borderColour);
            g.drawHorizontalLine(registeredLabel_.getY() - 4, left, right);
        }

        // Row separators between list entries
        if (!entries_.empty()) {
            g.setColour(borderColour.withAlpha(0.5f));
            auto listTop = listContainer_.getY();
            for (size_t i = 1; i < entries_.size(); ++i) {
                auto y = listTop + static_cast<int>(i) * kListRowHeight;
                g.drawHorizontalLine(y, left, right);
            }
        }
    }

    void load(const Config& config) {
        // Clear existing entries
        entries_.clear();
        for (auto& c : ownedDrawables_)
            listContainer_.removeChildComponent(c.get());
        for (auto& c : ownedLabels_)
            listContainer_.removeChildComponent(c.get());
        for (auto& c : ownedButtons_)
            listContainer_.removeChildComponent(c.get());
        ownedDrawables_.clear();
        ownedLabels_.clear();
        ownedButtons_.clear();

        for (const auto& [provider, key] : config.getAllAICredentials()) {
            if (key.empty())
                continue;
            if (!findProviderInfo(provider))
                continue;
            credentials_[provider] = juce::String(key);
            addListEntry(provider);
        }

        updateProviderComboState();
        resized();
    }

    void apply(Config& config) const {
        // Clear all credentials first
        for (const auto& p : getKnownProviders())
            config.setAICredential(p.id, "");

        // Write stored credentials
        for (const auto& [id, key] : credentials_)
            config.setAICredential(id, key.toStdString());
    }

    /** Return list of configured provider IDs (those with non-empty keys). */
    std::vector<std::string> getConfiguredProviders() const {
        std::vector<std::string> result;
        for (const auto& [id, key] : credentials_) {
            if (key.isNotEmpty())
                result.push_back(id);
        }
        return result;
    }

  private:
    static constexpr int kListRowHeight = 24;

    struct ListEntry {
        std::string providerId;
        juce::Drawable* iconComp = nullptr;
        juce::Label* nameLabel = nullptr;
        juce::Label* statusLabel = nullptr;
        juce::TextButton* removeBtn = nullptr;

        void layout(juce::Rectangle<int> bounds) {
            if (removeBtn)
                removeBtn->setBounds(bounds.removeFromRight(24).reduced(2, 1));
            if (statusLabel)
                statusLabel->setBounds(bounds.removeFromRight(80));
            if (iconComp)
                iconComp->setBounds(bounds.removeFromLeft(20).reduced(2, 4));
            bounds.removeFromLeft(4);
            if (nameLabel)
                nameLabel->setBounds(bounds);
        }
    };

    std::string getSelectedProviderId() const {
        int idx = providerCombo_.getSelectedId() - 1;
        const auto& providers = getKnownProviders();
        if (idx >= 0 && idx < static_cast<int>(providers.size()))
            return providers[static_cast<size_t>(idx)].id;
        return "";
    }

    void addCurrentProvider() {
        auto providerId = getSelectedProviderId();
        auto key = keyEditor_.getText().trim();

        if (providerId.empty() || key.isEmpty()) {
            statusLabel_.setText("Enter an API key first", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
            return;
        }

        // Store credential
        credentials_[providerId] = key;

        // Check if already in list, update; otherwise add
        bool found = false;
        for (auto& entry : entries_) {
            if (entry.providerId == providerId) {
                entry.statusLabel->setText("Updated", juce::dontSendNotification);
                entry.statusLabel->setColour(juce::Label::textColourId, juce::Colours::yellow);
                found = true;
                break;
            }
        }

        if (!found)
            addListEntry(providerId);

        keyEditor_.clear();
        statusLabel_.setText("Added", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        updateProviderComboState();
        resized();
    }

    void removeProvider(const std::string& providerId) {
        credentials_.erase(providerId);

        // Remove list entry
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->providerId == providerId) {
                listContainer_.removeChildComponent(it->iconComp);
                listContainer_.removeChildComponent(it->nameLabel);
                listContainer_.removeChildComponent(it->statusLabel);
                listContainer_.removeChildComponent(it->removeBtn);
                entries_.erase(it);
                updateProviderComboState();
                break;
            }
        }

        resized();
    }

    void addListEntry(const std::string& providerId) {
        auto* info = findProviderInfo(providerId);
        if (!info)
            return;

        ListEntry entry;
        entry.providerId = providerId;

        // Icon
        auto icon = createProviderIcon(*info);
        listContainer_.addAndMakeVisible(*icon);
        entry.iconComp = icon.get();
        ownedDrawables_.push_back(std::move(icon));

        // Name label
        auto nameLabel = std::make_unique<juce::Label>();
        nameLabel->setText(info->displayName, juce::dontSendNotification);
        styleLabel(*nameLabel, 12.0f);
        listContainer_.addAndMakeVisible(*nameLabel);
        entry.nameLabel = nameLabel.get();
        ownedLabels_.push_back(std::move(nameLabel));

        // Status label (masked key preview)
        auto statusLabel = std::make_unique<juce::Label>();
        auto key = credentials_[providerId];
        juce::String masked = key.substring(0, 4) + "..." + key.substring(key.length() - 4);
        statusLabel->setText(masked, juce::dontSendNotification);
        styleLabel(*statusLabel, 11.0f);
        statusLabel->setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_DIM));
        statusLabel->setJustificationType(juce::Justification::centredRight);
        listContainer_.addAndMakeVisible(*statusLabel);
        entry.statusLabel = statusLabel.get();
        ownedLabels_.push_back(std::move(statusLabel));

        // Remove button
        auto removeBtn =
            std::make_unique<juce::TextButton>(juce::String::charToString(0x2715));  // ✕
        auto pid = providerId;
        removeBtn->onClick = [this, pid]() { removeProvider(pid); };
        listContainer_.addAndMakeVisible(*removeBtn);
        entry.removeBtn = removeBtn.get();
        ownedButtons_.push_back(std::move(removeBtn));

        entries_.push_back(entry);
    }

    void updateProviderComboState() {
        const auto& providers = getKnownProviders();
        for (int i = 0; i < static_cast<int>(providers.size()); ++i) {
            bool registered = credentials_.count(providers[static_cast<size_t>(i)].id) > 0;
            providerCombo_.setItemEnabled(i + 1, !registered);
        }

        // If current selection is disabled, select the first enabled one
        auto selectedId = providerCombo_.getSelectedId();
        if (selectedId > 0 && !providerCombo_.isItemEnabled(selectedId)) {
            for (int i = 0; i < static_cast<int>(providers.size()); ++i) {
                if (providerCombo_.isItemEnabled(i + 1)) {
                    providerCombo_.setSelectedId(i + 1, juce::dontSendNotification);
                    break;
                }
            }
        }
    }

    void testKey() {
        auto providerId = getSelectedProviderId();
        auto key = keyEditor_.getText().trim();

        if (providerId.empty() || key.isEmpty()) {
            statusLabel_.setText("Enter an API key first", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::orange);
            return;
        }

        auto* info = findProviderInfo(providerId);
        if (!info)
            return;

        testBtn_.setEnabled(false);
        statusLabel_.setText("Testing...", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId,
                               DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

        auto testProvider = std::string(info->testProvider);
        auto testBaseUrl = std::string(info->testBaseUrl);
        auto testModel = std::string(info->testModel);
        auto safeThis = juce::Component::SafePointer<CloudPage>(this);

        juce::Thread::launch([safeThis, key, testProvider, testBaseUrl, testModel]() {
            Config::AgentLLMConfig cfg;
            cfg.provider = testProvider;
            cfg.baseUrl = testBaseUrl;
            cfg.apiKey = key.toStdString();
            cfg.model = testModel;

            auto pc = toLLMProviderConfig(cfg);
            auto client = llm::LLMClientFactory::create(pc);

            llm::Request req;
            req.systemPrompt = "Reply OK.";
            req.userMessage = "ping";
            req.temperature = 0.0f;

            auto resp = client->sendRequest(req);
            juce::String result;
            bool ok = false;
            if (resp.success) {
                ok = true;
                result = "OK (" + juce::String(resp.wallSeconds, 1) + "s)";
            } else {
                result = resp.error.substring(0, 50);
            }

            juce::MessageManager::callAsync([safeThis, ok, result]() {
                if (!safeThis)
                    return;
                safeThis->testBtn_.setEnabled(true);
                safeThis->statusLabel_.setText(result, juce::dontSendNotification);
                safeThis->statusLabel_.setColour(juce::Label::textColourId,
                                                 ok ? juce::Colours::limegreen
                                                    : juce::Colours::orange);
            });
        });
    }

    // Input area
    juce::Label providerLabel_, keyLabel_;
    juce::ComboBox providerCombo_;
    juce::TextEditor keyEditor_;
    juce::TextButton testBtn_, addBtn_;
    juce::Label statusLabel_;

    // Registered providers list
    juce::Label registeredLabel_;
    juce::Component listContainer_;
    std::vector<ListEntry> entries_;
    std::vector<std::unique_ptr<juce::Drawable>> ownedDrawables_;
    std::vector<std::unique_ptr<juce::Label>> ownedLabels_;
    std::vector<std::unique_ptr<juce::TextButton>> ownedButtons_;

    // Credential storage (provider ID → API key)
    std::map<std::string, juce::String> credentials_;
};

// ============================================================================
// LocalPage — embedded model configuration
// ============================================================================

class AISettingsDialog::LocalPage : public juce::Component {
  public:
    LocalPage() {
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

        gpuCombo_.addItem("Auto (GPU)", 1);
        gpuCombo_.addItem("CPU Only", 2);
        gpuCombo_.addItem("Custom", 3);
        styleCombo(gpuCombo_);
        gpuCombo_.onChange = [this]() { gpuComboChanged(); };
        addAndMakeVisible(gpuCombo_);

        styleEditor(gpuCustomEditor_, "32");
        gpuCustomEditor_.setInputRestrictions(4, "0123456789");
        gpuCustomEditor_.setVisible(false);
        addAndMakeVisible(gpuCustomEditor_);

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

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int labelW = 90;
        const int rowH = 26;

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
        gpuCombo_.setBounds(row.removeFromLeft(100).reduced(0, 1));
        if (gpuCustomEditor_.isVisible()) {
            row.removeFromLeft(4);
            gpuCustomEditor_.setBounds(row.removeFromLeft(40).reduced(0, 1));
        }
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
        setGpuLayers(config.getLocalLlamaGpuLayers());
        ctxEditor_.setText(juce::String(config.getLocalLlamaContextSize()),
                           juce::dontSendNotification);
        updateStatus();
    }

    void apply(Config& config) const {
        config.setLocalModelPath(modelEditor_.getText().toStdString());
        config.setLocalLlamaGpuLayers(getGpuLayers());
        config.setLocalLlamaContextSize(ctxEditor_.getText().getIntValue());
    }

    bool isModelLoaded() const {
        return LlamaModelManager::getInstance().isLoaded();
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
            cfg.gpuLayers = getGpuLayers();
            cfg.contextSize = ctxEditor_.getText().getIntValue();

            loadButton_.setEnabled(false);
            statusLabel_.setText("Loading...", juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::yellow);

            std::thread([this, cfg]() {
                bool ok = LlamaModelManager::getInstance().loadModel(cfg);
                juce::MessageManager::callAsync([this, ok, cfg]() {
                    loadButton_.setEnabled(true);
                    if (ok) {
                        auto& config = Config::getInstance();
                        config.setLocalModelPath(cfg.modelPath);
                        config.setLocalLlamaGpuLayers(cfg.gpuLayers);
                        config.setLocalLlamaContextSize(cfg.contextSize);
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

    void gpuComboChanged() {
        gpuCustomEditor_.setVisible(gpuCombo_.getSelectedId() == 3);
        resized();
    }

    void setGpuLayers(int layers) {
        if (layers < 0) {
            gpuCombo_.setSelectedId(1, juce::dontSendNotification);  // Auto (GPU)
        } else if (layers == 0) {
            gpuCombo_.setSelectedId(2, juce::dontSendNotification);  // CPU Only
        } else {
            gpuCombo_.setSelectedId(3, juce::dontSendNotification);  // Custom
            gpuCustomEditor_.setText(juce::String(layers), juce::dontSendNotification);
        }
        gpuCustomEditor_.setVisible(gpuCombo_.getSelectedId() == 3);
    }

    int getGpuLayers() const {
        switch (gpuCombo_.getSelectedId()) {
            case 1:
                return -1;  // Auto (GPU)
            case 2:
                return 0;  // CPU Only
            case 3:
                return gpuCustomEditor_.getText().getIntValue();  // Custom
            default:
                return -1;
        }
    }

    juce::Label modelLabel_, gpuLabel_, ctxLabel_;
    juce::TextEditor modelEditor_, gpuCustomEditor_, ctxEditor_;
    juce::ComboBox gpuCombo_;
    juce::TextButton browseButton_, downloadButton_, loadButton_;
    juce::Label statusLabel_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<ModelDownloader> downloader_;
};

// ============================================================================
// ConfigPage — Simple (preset) / Advanced (per-agent mapping)
//
// Provider combos are populated dynamically from the Cloud/Local pages
// so there is no duplication of provider selection.
// ============================================================================

class AISettingsDialog::ConfigPage : public juce::Component {
  public:
    CloudPage* cloudPage = nullptr;
    LocalPage* localPage = nullptr;

    ConfigPage() {
        // Mode selector: Local / Cloud / Hybrid
        modeLabel_.setText("Mode", juce::dontSendNotification);
        styleLabel(modeLabel_);
        addAndMakeVisible(modeLabel_);

        modeCombo_.addItem("Local", 1);
        modeCombo_.addItem("Cloud", 2);
        modeCombo_.addItem("Hybrid", 3);
        styleCombo(modeCombo_);
        modeCombo_.onChange = [this]() { updateModeUI(); };
        addAndMakeVisible(modeCombo_);

        // Provider selector (cloud/hybrid)
        providerLabel_.setText("Provider", juce::dontSendNotification);
        styleLabel(providerLabel_);
        addAndMakeVisible(providerLabel_);
        styleCombo(providerCombo_);
        addAndMakeVisible(providerCombo_);

        // Optimize selector (cloud/hybrid)
        optimizeLabel_.setText("Optimize", juce::dontSendNotification);
        styleLabel(optimizeLabel_);
        addAndMakeVisible(optimizeLabel_);
        styleCombo(optimizeCombo_);
        addAndMakeVisible(optimizeCombo_);

        // Local model name label
        modelNameLabel_.setText("No model loaded", juce::dontSendNotification);
        styleLabel(modelNameLabel_);
        modelNameLabel_.setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_DIM));
        addAndMakeVisible(modelNameLabel_);

        modeCombo_.setSelectedId(1, juce::dontSendNotification);
        updateModeUI();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int rowH = 28;
        const int labelW = 80;

        // Mode row
        auto row = bounds.removeFromTop(rowH);
        modeLabel_.setBounds(row.removeFromLeft(labelW));
        modeCombo_.setBounds(row.reduced(0, 2));
        bounds.removeFromTop(8);

        int mode = modeCombo_.getSelectedId();

        if (mode == 1) {
            // Local: show model name
            modelNameLabel_.setBounds(bounds.removeFromTop(rowH).withTrimmedLeft(labelW));
        } else {
            // Cloud/Hybrid: provider + optimize
            row = bounds.removeFromTop(rowH);
            providerLabel_.setBounds(row.removeFromLeft(labelW));
            providerCombo_.setBounds(row.reduced(0, 2));
            bounds.removeFromTop(4);

            row = bounds.removeFromTop(rowH);
            optimizeLabel_.setBounds(row.removeFromLeft(labelW));
            optimizeCombo_.setBounds(row.reduced(0, 2));
        }
    }

    void refreshProviderCombos() {
        providerCombo_.clear();
        int nextId = 1;
        if (cloudPage) {
            for (const auto& pid : cloudPage->getConfiguredProviders()) {
                auto* info = findProviderInfo(pid);
                if (info)
                    providerCombo_.addItem(info->displayName, nextId++);
            }
        }
        // Re-select saved provider
        if (savedProviderDisplay_.isNotEmpty()) {
            for (int i = 0; i < providerCombo_.getNumItems(); ++i) {
                if (providerCombo_.getItemText(i) == savedProviderDisplay_) {
                    providerCombo_.setSelectedId(providerCombo_.getItemId(i),
                                                 juce::dontSendNotification);
                    return;
                }
            }
        }
        if (providerCombo_.getNumItems() > 0)
            providerCombo_.setSelectedId(providerCombo_.getItemId(0), juce::dontSendNotification);
    }

    void load(const Config& config) {
        auto presetId = config.getAIPreset();

        // Determine mode from preset
        if (presetId.starts_with("local") || presetId == "local_embedded") {
            modeCombo_.setSelectedId(1, juce::dontSendNotification);
        } else if (presetId.starts_with("hybrid")) {
            modeCombo_.setSelectedId(3, juce::dontSendNotification);
            if (presetId == "hybrid_speed")
                savedOptimize_ = "Speed";
            else
                savedOptimize_ = "Cost";
        } else {
            modeCombo_.setSelectedId(2, juce::dontSendNotification);
            // Infer optimize from whether command uses cloud
            auto cmdCfg = config.getAgentLLMConfig("command");
            auto musicCfg = config.getAgentLLMConfig("music");
            if (cmdCfg.provider == musicCfg.provider && cmdCfg.model == musicCfg.model)
                savedOptimize_ = "Quality";
            else
                savedOptimize_ = "Cost";
        }

        // Determine provider from music agent config
        auto musicCfg = config.getAgentLLMConfig("music");
        if (musicCfg.provider == "anthropic")
            savedProviderDisplay_ = "Anthropic";
        else if (musicCfg.provider == "gemini")
            savedProviderDisplay_ = "Gemini";
        else if (musicCfg.provider == "deepseek")
            savedProviderDisplay_ = "DeepSeek";
        else if (musicCfg.provider == "openrouter")
            savedProviderDisplay_ = "OpenRouter";
        else if (musicCfg.provider == "openai_chat")
            savedProviderDisplay_ = "OpenAI";

        // Update local model label
        auto modelPath = config.getLocalModelPath();
        if (!modelPath.empty()) {
            auto filename = juce::File(juce::String(modelPath)).getFileName();
            modelNameLabel_.setText(filename, juce::dontSendNotification);
        } else {
            modelNameLabel_.setText("No model configured", juce::dontSendNotification);
        }

        updateModeUI();
    }

    void apply(Config& config) const {
        int mode = modeCombo_.getSelectedId();
        auto providerStr = providerDisplayToId(providerCombo_.getText());
        auto optimize = optimizeCombo_.getText();

        if (mode == 1) {
            // Local
            config.setAIPreset("local_embedded");
            auto* preset = magda::findPreset("local_embedded");
            if (preset)
                for (const auto& [role, cfg] : preset->agents)
                    config.setAgentLLMConfig(role, cfg);
        } else if (mode == 2) {
            // Cloud
            std::string presetId = "cloud_" + providerStr;
            config.setAIPreset(presetId);

            auto* preset = magda::findPreset(presetId);
            if (preset) {
                for (const auto& [role, presetCfg] : preset->agents) {
                    auto cfg = presetCfg;
                    cfg.apiKey = "";
                    config.setAgentLLMConfig(role, cfg);
                }
            }

            // Cost optimization: use cheaper models for router+command
            if (optimize == "Cost") {
                auto routerCfg = config.getAgentLLMConfig("router");
                auto cmdCfg = config.getAgentLLMConfig("command");
                applyCheaperModel(routerCfg, providerStr);
                applyCheaperModel(cmdCfg, providerStr);
                config.setAgentLLMConfig("router", routerCfg);
                config.setAgentLLMConfig("command", cmdCfg);
            }
        } else {
            // Hybrid
            std::string presetId = optimize == "Speed" ? "hybrid_speed" : "hybrid_cost";
            config.setAIPreset(presetId);

            // Router is always local
            Config::AgentLLMConfig localCfg;
            localCfg.provider = "llama_local";
            config.setAgentLLMConfig("router", localCfg);

            // Music always uses cloud
            auto musicCfg = makeCloudConfig("music", providerStr);
            config.setAgentLLMConfig("music", musicCfg);

            // Command: cloud for speed, local for cost
            if (optimize == "Speed") {
                auto cmdCfg = makeCloudConfig("command", providerStr);
                config.setAgentLLMConfig("command", cmdCfg);
            } else {
                Config::AgentLLMConfig cmdLocal;
                cmdLocal.provider = "llama_local";
                config.setAgentLLMConfig("command", cmdLocal);
            }
        }
    }

  private:
    void updateModeUI() {
        int mode = modeCombo_.getSelectedId();
        bool isLocal = (mode == 1);
        bool isCloud = (mode == 2);
        bool isHybrid = (mode == 3);

        modelNameLabel_.setVisible(isLocal);
        providerLabel_.setVisible(!isLocal);
        providerCombo_.setVisible(!isLocal);
        optimizeLabel_.setVisible(!isLocal);
        optimizeCombo_.setVisible(!isLocal);

        // Update optimize options based on mode
        optimizeCombo_.clear();
        if (isCloud) {
            optimizeCombo_.addItem("Quality", 1);
            optimizeCombo_.addItem("Cost", 2);
        } else if (isHybrid) {
            optimizeCombo_.addItem("Speed", 1);
            optimizeCombo_.addItem("Cost", 2);
        }

        // Restore saved optimize selection
        if (!isLocal) {
            refreshProviderCombos();
            bool found = false;
            for (int i = 0; i < optimizeCombo_.getNumItems(); ++i) {
                if (optimizeCombo_.getItemText(i) == savedOptimize_) {
                    optimizeCombo_.setSelectedId(optimizeCombo_.getItemId(i),
                                                 juce::dontSendNotification);
                    found = true;
                    break;
                }
            }
            if (!found)
                optimizeCombo_.setSelectedId(1, juce::dontSendNotification);
        }

        resized();
    }

    static std::string providerDisplayToId(const juce::String& display) {
        if (display == "Anthropic")
            return "anthropic";
        if (display == "Gemini")
            return "gemini";
        if (display == "DeepSeek")
            return "deepseek";
        if (display == "OpenRouter")
            return "openrouter";
        return "openai_chat";
    }

    static Config::AgentLLMConfig makeCloudConfig(const std::string& role,
                                                  const std::string& provider) {
        // Look up from preset for correct model assignments
        std::string presetId = "cloud_" + provider;
        if (auto* preset = magda::findPreset(presetId)) {
            auto it = preset->agents.find(role);
            if (it != preset->agents.end()) {
                auto cfg = it->second;
                cfg.apiKey = "";
                return cfg;
            }
        }
        // Fallback
        Config::AgentLLMConfig cfg;
        cfg.provider = provider;
        return cfg;
    }

    static void applyCheaperModel(Config::AgentLLMConfig& cfg, const std::string& provider) {
        if (provider == "openai_chat")
            cfg.model = "gpt-4.1-mini";
        else if (provider == "anthropic")
            cfg.model = "claude-haiku-4-5-20251001";
        else if (provider == "gemini")
            cfg.model = "gemini-2.0-flash";
        else if (provider == "deepseek")
            cfg.model = "deepseek-chat";
        // openrouter: keep default
    }

    juce::Label modeLabel_;
    juce::ComboBox modeCombo_;
    juce::Label providerLabel_;
    juce::ComboBox providerCombo_;
    juce::Label optimizeLabel_;
    juce::ComboBox optimizeCombo_;
    juce::Label modelNameLabel_;
    juce::String savedProviderDisplay_;
    juce::String savedOptimize_ = "Quality";
};

// ============================================================================
// AISettingsDialog
// ============================================================================

AISettingsDialog::AISettingsDialog() {
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    cloudPage_ = std::make_unique<CloudPage>();
    localPage_ = std::make_unique<LocalPage>();
    configPage_ = std::make_unique<ConfigPage>();

    // Wire config page to sibling pages
    configPage_->cloudPage = cloudPage_.get();
    configPage_->localPage = localPage_.get();

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabbedComponent_.addTab("Cloud", tabBg, cloudPage_.get(), false);
    tabbedComponent_.addTab("Local", tabBg, localPage_.get(), false);
    tabbedComponent_.addTab("Config", tabBg, configPage_.get(), false);

    // Refresh config combos when switching to Config tab
    tabbedComponent_.onTabChanged = [this](int tabIndex) {
        if (tabIndex == 2)
            configPage_->refreshProviderCombos();
    };

    addAndMakeVisible(tabbedComponent_);

    okBtn_.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible(okBtn_);

    cancelBtn_.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->closeButtonPressed();
    };
    addAndMakeVisible(cancelBtn_);

    loadSettings();

    setSize(540, 480);
}

AISettingsDialog::~AISettingsDialog() {
    setLookAndFeel(nullptr);
}

void AISettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void AISettingsDialog::resized() {
    auto bounds = getLocalBounds().reduced(8);

    auto buttonRow = bounds.removeFromBottom(36);
    cancelBtn_.setBounds(buttonRow.removeFromRight(80).reduced(0, 4));
    buttonRow.removeFromRight(8);
    okBtn_.setBounds(buttonRow.removeFromRight(80).reduced(0, 4));
    bounds.removeFromBottom(4);

    tabbedComponent_.setBounds(bounds);
}

void AISettingsDialog::loadSettings() {
    auto& config = Config::getInstance();
    cloudPage_->load(config);
    localPage_->load(config);
    configPage_->load(config);
}

void AISettingsDialog::applySettings() {
    auto& config = Config::getInstance();
    cloudPage_->apply(config);
    localPage_->apply(config);
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
