#include "AISettingsPages.hpp"

#include <juce_llm/juce_llm.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

#include "../../../../agents/llama_model_manager.hpp"
#include "../../../../agents/llm_config_utils.hpp"
#include "../../../../agents/llm_presets.hpp"
#include "../../../../agents/model_downloader.hpp"
#include "../../../../agents/openai_url.hpp"
#include "../../../core/AppPaths.hpp"
#include "../../../core/Config.hpp"
#include "../../../core/StringTable.hpp"
#include "../../../media_db/MediaDbContext.hpp"
#include "../../../media_db/MediaModelDownloader.hpp"
#include "../../../stem_separation/DemucsSeparator.hpp"
#include "../../../stem_separation/StemModelDownloader.hpp"
#include "../../themes/ActiveTheme.hpp"
#include "../../themes/DialogLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "SettingsSections.hpp"
#include "SettingsToolbar.hpp"
#include "magda/agents/command_model_downloader.hpp"
#include "ui/components/common/MagdaAlertWindow.hpp"
#include "ui/components/common/PillSegments.hpp"
#include "ui/themes/SettingsLookAndFeel.hpp"

namespace magda {

// ============================================================================
// Helpers
// ============================================================================

namespace {

void styleLabel(juce::Label& label, float size = 12.0f) {
    label.setFont(FontManager::getInstance().getUIFont(size));
    label.setColour(juce::Label::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
}

void styleEditor(juce::TextEditor& ed, const juce::String& placeholder, bool password = false) {
    ed.setFont(FontManager::getInstance().getUIFont(12.0f));
    ed.setTextToShowWhenEmpty(placeholder, ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    ed.setColour(juce::TextEditor::backgroundColourId,
                 ActiveTheme::getColour(ActiveTheme::SURFACE));
    ed.setColour(juce::TextEditor::textColourId, ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    ed.setColour(juce::TextEditor::outlineColourId, ActiveTheme::getColour(ActiveTheme::BORDER));
    if (password)
        ed.setPasswordCharacter(static_cast<juce::juce_wchar>('*'));
}

void styleCombo(juce::ComboBox& combo) {
    combo.setColour(juce::ComboBox::backgroundColourId,
                    ActiveTheme::getColour(ActiveTheme::SURFACE));
    combo.setColour(juce::ComboBox::textColourId,
                    ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    combo.setColour(juce::ComboBox::outlineColourId, ActiveTheme::getColour(ActiveTheme::BORDER));
}

/// Segments that show a combo box's items; the combo stays the state and keeps its onChange.
class ComboSegments : public daw::ui::PillSegments, private juce::Timer {
  public:
    explicit ComboSegments(juce::ComboBox& combo) : combo_(combo) {
        onChange = [this](int index) {
            if (juce::isPositiveAndBelow(index, ids_.size()))
                combo_.setSelectedId(ids_[index], juce::sendNotificationSync);
        };
        sync();
        startTimerHz(10);
    }

    // Polled: the combo's items and visibility change from many places.
    void sync() {
        juce::StringArray options;
        juce::Array<int> ids;
        for (int i = 0; i < combo_.getNumItems(); ++i) {
            options.add(combo_.getItemText(i));
            ids.add(combo_.getItemId(i));
        }
        if (options != options_) {
            options_ = options;
            setOptions(options);
        }
        ids_ = ids;
        setSelectedIndex(ids_.indexOf(combo_.getSelectedId()));
        setVisible(combo_.isVisible());
    }

  private:
    void timerCallback() override {
        sync();
    }

    juce::ComboBox& combo_;
    juce::StringArray options_;
    juce::Array<int> ids_;
};

/// Whether npx can be found where a GUI-launched process would look for it.
bool npxAvailable() {
#if JUCE_WINDOWS
    const juce::StringArray names{"npx.cmd", "npx.exe"};
    const auto separator = ";";
#else
    const juce::StringArray names{"npx"};
    const auto separator = ":";
#endif
    auto dirs = juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}),
                                              separator, {});
    // A macOS app gets a short PATH; Homebrew and the Node installer live here.
    dirs.addArray({"/opt/homebrew/bin", "/usr/local/bin"});
    for (const auto& dir : dirs)
        for (const auto& name : names)
            if (dir.isNotEmpty() && juce::File(dir).getChildFile(name).existsAsFile())
                return true;
    return false;
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
        {magda::provider::OPENAI_CHAT, "OpenAI", magda::provider::OPENAI_CHAT, "",
         magda::model::GPT_4_1_MINI, BinaryData::openai_svg, BinaryData::openai_svgSize},
        {magda::provider::ANTHROPIC, "Anthropic", magda::provider::ANTHROPIC, "",
         magda::model::CLAUDE_HAIKU, BinaryData::anthropic_svg, BinaryData::anthropic_svgSize},
        {magda::provider::GEMINI, "Gemini", magda::provider::GEMINI, "", magda::model::GEMINI_FLASH,
         BinaryData::gemini_svg, BinaryData::gemini_svgSize},
        {magda::provider::DEEPSEEK, "DeepSeek", magda::provider::DEEPSEEK, "",
         magda::model::DEEPSEEK_CHAT, BinaryData::deepseek_svg, BinaryData::deepseek_svgSize},
        {magda::provider::OPENROUTER, "OpenRouter", magda::provider::OPENROUTER, "",
         magda::model::LLAMA_70B, BinaryData::openrouter_svg, BinaryData::openrouter_svgSize},
    };
    return providers;
}

/// A Drawable together with the Component that displays it.
struct ProviderIcon {
    explicit ProviderIcon(std::unique_ptr<juce::Drawable> d)
        : drawable(std::move(d)), component(*drawable) {}

    std::unique_ptr<juce::Drawable> drawable;
    juce::DrawableComponent component;
};

std::unique_ptr<juce::Drawable> createProviderIcon(const ProviderInfo& info) {
    auto icon =
        juce::Drawable::createFromImageData(info.iconData, static_cast<size_t>(info.iconDataSize));
    if (icon)
        ActiveTheme::applyToSvgIcon(*icon);
    return icon;
}

const ProviderInfo* findProviderInfo(const std::string& id) {
    for (const auto& p : getKnownProviders())
        if (p.id == id)
            return &p;
    return nullptr;
}

// Known model ids offered per provider in the Advanced grid. The model combos
// stay editable, so this is a convenience list, not a hard constraint. Local
// providers return {} - embedded uses the loaded GGUF, local-server models are
// entered/probed on the Config Local-server picker.
std::vector<juce::String> knownModelsForProvider(const std::string& providerId) {
    namespace m = magda::model;
    if (providerId == magda::provider::OPENAI_CHAT ||
        providerId == magda::provider::OPENAI_RESPONSES)
        return {m::GPT_5_6_SOL, m::GPT_5_6_TERRA, m::GPT_5_6_LUNA, m::GPT_5_5_PRO,  m::GPT_5_5,
                m::GPT_5_4_PRO, m::GPT_5_4,       m::GPT_5_4_MINI, m::GPT_5_4_NANO, m::GPT_5_2,
                m::GPT_5_1,     m::GPT_5,         m::GPT_5_MINI,   m::GPT_5_NANO,   m::O3_PRO,
                m::O3,          m::GPT_4_1,       m::GPT_4_1_MINI};
    if (providerId == magda::provider::ANTHROPIC)
        return {m::CLAUDE_FABLE_5,  m::CLAUDE_OPUS_4_8,   m::CLAUDE_OPUS_4_7,
                m::CLAUDE_SONNET_5, m::CLAUDE_SONNET_4_6, m::CLAUDE_HAIKU};
    if (providerId == magda::provider::GEMINI)
        return {m::GEMINI_3_5_FLASH,      m::GEMINI_3_1_PRO, m::GEMINI_3_1_FLASH,
                m::GEMINI_3_1_FLASH_LITE, m::GEMINI_2_5_PRO, m::GEMINI_2_5_FLASH};
    if (providerId == magda::provider::DEEPSEEK)
        return {m::DEEPSEEK_V4_PRO, m::DEEPSEEK_V4_FLASH};
    if (providerId == magda::provider::OPENROUTER)
        return {m::LLAMA_70B};
    return {};
}

// Result of probing an OpenAI-compatible server's GET /v1/models endpoint.
struct ModelProbeResult {
    bool ok = false;
    int statusCode = 0;
    std::vector<juce::String> models;  // opaque ids, in server order
    juce::String error;
};

// Blocking GET {normalized base}/models with an optional bearer token. Safe to
// call off the message thread. Model ids are treated as opaque.
ModelProbeResult probeLocalServerModels(const juce::String& rawBaseUrl,
                                        const juce::String& apiKey) {
    ModelProbeResult result;
    auto base = juce::String(magda::normalizeOpenAIBaseUrl(rawBaseUrl.toStdString()));
    if (base.isEmpty()) {
        result.error = "Enter a base URL";
        return result;
    }

    juce::URL url(base + "/models");
    int statusCode = 0;
    // InputStreamOptions holds references and has no copy-assignment, so the
    // bearer header must be folded into the single construction chain. An empty
    // extra-headers string is a harmless no-op.
    const juce::String headers =
        apiKey.isNotEmpty() ? ("Authorization: Bearer " + apiKey) : juce::String();
    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs(5000)
                       .withStatusCode(&statusCode)
                       .withExtraHeaders(headers);

    auto stream = url.createInputStream(options);
    result.statusCode = statusCode;
    if (stream == nullptr) {
        result.error = "Connection failed";
        return result;
    }
    if (statusCode < 200 || statusCode >= 300) {
        result.error = "HTTP " + juce::String(statusCode);
        return result;
    }

    auto parsed = juce::JSON::parse(stream->readEntireStreamAsString());
    if (auto* obj = parsed.getDynamicObject()) {
        if (auto* arr = obj->getProperty("data").getArray()) {
            for (auto& item : *arr) {
                if (auto* io = item.getDynamicObject()) {
                    auto id = io->getProperty("id").toString();
                    if (id.isNotEmpty())
                        result.models.push_back(id);
                }
            }
        }
    }
    result.ok = true;
    return result;
}

}  // namespace

// ============================================================================
// CloudPage — manage cloud provider API keys
//
// Top: provider combo + API key field + Test + Add buttons
// Bottom: compact list of registered providers with remove buttons
// ============================================================================

class AISettingsPages::CloudPage : public juce::Component {
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
        providerLabel_.setVisible(false);

        styleEditor(keyEditor_, "Paste API key", true);
        keyEditor_.setIndents(8, 6);
        addAndMakeVisible(keyEditor_);

        // Test button
        testBtn_.setButtonText("Test");
        testBtn_.onClick = [this]() { testKey(); };
        addAndMakeVisible(testBtn_);

        // Status label
        styleLabel(statusLabel_, 11.0f);
        statusLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        addAndMakeVisible(statusLabel_);

        // Add/Save button
        addBtn_.setButtonText("Add");
        addBtn_.onClick = [this]() { addCurrentProvider(); };
        MagdaAlertWindow::setRole(addBtn_, MagdaAlertWindow::ButtonRole::Primary);
        addAndMakeVisible(addBtn_);

        registeredLabel_.setText("API keys", juce::dontSendNotification);
        daw::ui::SettingsLookAndFeel::markSectionHeader(registeredLabel_);
        addAndMakeVisible(registeredLabel_);

        footnoteLabel_.setText("Keys are saved in MAGDA's settings file. Test sends one small "
                               "request to the provider to check that the key works.",
                               juce::dontSendNotification);
        styleLabel(footnoteLabel_, 11.5f);
        footnoteLabel_.setColour(juce::Label::textColourId,
                                 ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        footnoteLabel_.setJustificationType(juce::Justification::topLeft);
        footnoteLabel_.setBorderSize({});
        addAndMakeVisible(footnoteLabel_);

        addAndMakeVisible(listContainer_);
    }

    juce::Component& getToolbar() {
        return badge_;
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(0, 8);
        registeredLabel_.setBounds(bounds.removeFromTop(30));

        listContainer_.setBounds(
            bounds.removeFromTop(static_cast<int>(entries_.size()) * kListRowHeight));
        auto listBounds = listContainer_.getLocalBounds();
        for (auto& entry : entries_)
            entry.layout(listBounds.removeFromTop(kListRowHeight));

        // The add row: provider, key, Test, Add.
        addRow_ = bounds.removeFromTop(46);
        auto row = addRow_.reduced(0, 8);
        addBtn_.setBounds(row.removeFromRight(60));
        row.removeFromRight(6);
        testBtn_.setBounds(row.removeFromRight(60));
        row.removeFromRight(8);
        providerCombo_.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(8);
        keyEditor_.setBounds(row);
        statusLabel_.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(6);
        footnoteLabel_.setBounds(bounds.removeFromTop(40));

        const auto count = static_cast<int>(entries_.size());
        badge_.setStatus(count == 0   ? juce::String("No keys added")
                         : count == 1 ? juce::String("1 key added")
                                      : juce::String(count) + " keys added",
                         count > 0);
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        const auto top = listContainer_.getY();
        for (size_t i = 1; i <= entries_.size(); ++i)
            g.fillRect(0, top + static_cast<int>(i) * kListRowHeight - 1, getWidth(), 1);
        g.fillRect(0, addRow_.getBottom() - 1, getWidth(), 1);
    }

    void load(const Config& config) {
        // Clear existing entries
        entries_.clear();
        for (auto& c : ownedDrawables_)
            listContainer_.removeChildComponent(&c->component);
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
    static constexpr int kListRowHeight = 48;

    struct ListEntry {
        std::string providerId;
        juce::DrawableComponent* iconComp = nullptr;
        juce::Label* nameLabel = nullptr;
        juce::Label* statusLabel = nullptr;  // the masked key, under the name
        juce::Label* resultLabel = nullptr;  // what the last Test said
        juce::TextButton* testBtn = nullptr;
        juce::TextButton* removeBtn = nullptr;

        void layout(juce::Rectangle<int> bounds) {
            auto right = bounds.removeFromRight(200);
            if (removeBtn)
                removeBtn->setBounds(right.removeFromRight(60).reduced(0, 12));
            right.removeFromRight(4);
            if (testBtn)
                testBtn->setBounds(right.removeFromRight(60).reduced(0, 9));
            if (resultLabel)
                resultLabel->setBounds(bounds.removeFromRight(160));
            if (iconComp)
                iconComp->setTransformToFit(bounds.removeFromLeft(28).reduced(4, 12).toFloat(),
                                            juce::RectanglePlacement::centred);
            bounds.removeFromLeft(8);
            if (nameLabel)
                nameLabel->setBounds(bounds.removeFromTop(26).withTrimmedTop(7));
            if (statusLabel)
                statusLabel->setBounds(bounds.removeFromTop(16));
        }
    };

    /** @brief The registered-list row for a provider, or entries_.end(). */
    auto findListEntry(const std::string& providerId) {
        const auto isForProvider = [&providerId](const ListEntry& entry) {
            return entry.providerId == providerId;
        };
        return std::ranges::find_if(entries_, isForProvider);
    }

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

        if (auto entry = findListEntry(providerId); entry != entries_.end()) {
            entry->resultLabel->setText("Updated", juce::dontSendNotification);
            entry->resultLabel->setColour(juce::Label::textColourId, juce::Colours::yellow);
        } else {
            addListEntry(providerId);
        }

        keyEditor_.clear();
        statusLabel_.setText("Added", juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        updateProviderComboState();
        resized();
    }

    void removeProvider(const std::string& providerId) {
        credentials_.erase(providerId);

        if (auto entry = findListEntry(providerId); entry != entries_.end()) {
            listContainer_.removeChildComponent(entry->iconComp);
            listContainer_.removeChildComponent(entry->nameLabel);
            listContainer_.removeChildComponent(entry->statusLabel);
            listContainer_.removeChildComponent(entry->removeBtn);
            listContainer_.removeChildComponent(entry->testBtn);
            listContainer_.removeChildComponent(entry->resultLabel);
            entries_.erase(entry);
            updateProviderComboState();
        }

        resized();
    }

    void addListEntry(const std::string& providerId) {
        const auto* info = findProviderInfo(providerId);
        if (!info)
            return;

        ListEntry entry;
        entry.providerId = providerId;

        // Icon
        if (auto drawable = createProviderIcon(*info)) {
            auto icon = std::make_unique<ProviderIcon>(std::move(drawable));
            listContainer_.addAndMakeVisible(icon->component);
            entry.iconComp = &icon->component;
            ownedDrawables_.push_back(std::move(icon));
        }

        // Name label
        auto nameLabel = std::make_unique<juce::Label>();
        nameLabel->setText(info->displayName, juce::dontSendNotification);
        styleLabel(*nameLabel, 13.0f);
        listContainer_.addAndMakeVisible(*nameLabel);
        entry.nameLabel = nameLabel.get();
        ownedLabels_.push_back(std::move(nameLabel));

        // Status label (masked key preview)
        auto statusLabel = std::make_unique<juce::Label>();
        auto key = credentials_[providerId];
        juce::String masked = key.substring(0, 4) + "..." + key.substring(key.length() - 4);
        statusLabel->setText(masked, juce::dontSendNotification);
        statusLabel->setFont(FontManager::getInstance().getMonoFont(11.0f));
        statusLabel->setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        listContainer_.addAndMakeVisible(*statusLabel);
        entry.statusLabel = statusLabel.get();
        ownedLabels_.push_back(std::move(statusLabel));

        auto resultLabel = std::make_unique<juce::Label>();
        styleLabel(*resultLabel, 11.5f);
        resultLabel->setJustificationType(juce::Justification::centredRight);
        listContainer_.addAndMakeVisible(*resultLabel);
        entry.resultLabel = resultLabel.get();
        ownedLabels_.push_back(std::move(resultLabel));

        auto testBtn = std::make_unique<juce::TextButton>("Test");
        const auto& pid = providerId;
        testBtn->onClick = [this, pid]() { testStoredKey(pid); };
        listContainer_.addAndMakeVisible(*testBtn);
        entry.testBtn = testBtn.get();
        ownedButtons_.push_back(std::move(testBtn));

        auto removeBtn = std::make_unique<juce::TextButton>("Remove");
        daw::ui::SettingsLookAndFeel::markLinkButton(*removeBtn);
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
            const auto isEnabled = [this](int itemId) {
                return providerCombo_.isItemEnabled(itemId);
            };
            const auto itemIds = std::views::iota(1, static_cast<int>(providers.size()) + 1);
            if (const auto first = std::ranges::find_if(itemIds, isEnabled); first != itemIds.end())
                providerCombo_.setSelectedId(*first, juce::dontSendNotification);
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
        runTest(providerId, key, testBtn_, statusLabel_);
    }

    void testStoredKey(const std::string& providerId) {
        auto entry = findListEntry(providerId);
        if (entry == entries_.end() || credentials_.count(providerId) == 0)
            return;
        runTest(providerId, credentials_[providerId], *entry->testBtn, *entry->resultLabel);
    }

    /// One small request with @p key; the result lands in @p result.
    void runTest(const std::string& providerId, const juce::String& key, juce::TextButton& button,
                 juce::Label& result) {
        const auto* info = findProviderInfo(providerId);
        if (!info)
            return;

        button.setEnabled(false);
        result.setText("Testing...", juce::dontSendNotification);
        result.setColour(juce::Label::textColourId,
                         ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        auto safeButton = juce::Component::SafePointer<juce::TextButton>(&button);
        auto safeResult = juce::Component::SafePointer<juce::Label>(&result);

        auto testProvider = std::string(info->testProvider);
        auto testBaseUrl = std::string(info->testBaseUrl);
        auto testModel = std::string(info->testModel);
        auto safeThis = juce::Component::SafePointer<CloudPage>(this);

        juce::Thread::launch([safeThis, safeButton, safeResult, key, testProvider, testBaseUrl,
                              testModel]() {
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

            juce::MessageManager::callAsync([safeThis, safeButton, safeResult, ok, result]() {
                if (!safeThis || !safeButton || !safeResult)
                    return;
                safeButton->setEnabled(true);
                safeResult->setText(
                    ok ? "Verified " + result.fromFirstOccurrenceOf("OK ", false, false) : result,
                    juce::dontSendNotification);
                safeResult->setColour(juce::Label::textColourId,
                                      ok ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
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
    juce::Label footnoteLabel_;
    juce::Rectangle<int> addRow_;
    SettingsStatusBadge badge_;
    juce::Component listContainer_;
    std::vector<ListEntry> entries_;
    std::vector<std::unique_ptr<ProviderIcon>> ownedDrawables_;
    std::vector<std::unique_ptr<juce::Label>> ownedLabels_;
    std::vector<std::unique_ptr<juce::TextButton>> ownedButtons_;

    // Credential storage (provider ID → API key)
    std::map<std::string, juce::String> credentials_;
};

// ============================================================================
// LocalPage — embedded model configuration
// ============================================================================

class AISettingsPages::LocalPage : public juce::Component {
  public:
    LocalPage() {
        // Download model button
        downloadButton_.setButtonText(
            juce::String(juce::CharPointer_UTF8("Download a model\xe2\x80\xa6")));
        downloadButton_.onClick = [this]() { startDownload(); };
        addAndMakeVisible(downloadButton_);

        // Model file
        modelLabel_.setText("File (.gguf)", juce::dontSendNotification);
        styleLabel(modelLabel_, 13.0f);
        addAndMakeVisible(modelLabel_);

        styleEditor(modelEditor_, "/path/to/model.gguf");
        addAndMakeVisible(modelEditor_);

        browseButton_.setButtonText(juce::String(juce::CharPointer_UTF8("Choose\xe2\x80\xa6")));
        browseButton_.onClick = [this]() { browseModel(); };
        addAndMakeVisible(browseButton_);

        // GPU layers
        gpuLabel_.setText("GPU layers", juce::dontSendNotification);
        styleLabel(gpuLabel_, 13.0f);
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
        ctxLabel_.setText("Context length", juce::dontSendNotification);
        styleLabel(ctxLabel_, 13.0f);
        tokensLabel_.setText("tokens", juce::dontSendNotification);
        styleLabel(tokensLabel_, 11.5f);
        tokensLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        addAndMakeVisible(tokensLabel_);
        addAndMakeVisible(ctxLabel_);

        styleEditor(ctxEditor_, "4096");
        ctxEditor_.setJustification(juce::Justification::centredRight);
        ctxEditor_.setIndents(8, 6);
        ctxEditor_.setInputRestrictions(6, "0123456789");
        addAndMakeVisible(ctxEditor_);

        // Load/Unload button
        loadButton_.setButtonText("Load model");
        loadButton_.onClick = [this]() { toggleModel(); };
        MagdaAlertWindow::setRole(loadButton_, MagdaAlertWindow::ButtonRole::Primary);
        addAndMakeVisible(loadButton_);

        // Status
        styleLabel(statusLabel_, 11.0f);
        statusLabel_.setColour(juce::Label::textColourId,
                               ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        addAndMakeVisible(statusLabel_);

        // Load on startup toggle
        loadOnStartupToggle_.setButtonText("Load model on startup");
        loadOnStartupToggle_.setColour(juce::ToggleButton::textColourId,
                                       ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        addAndMakeVisible(loadOnStartupToggle_);

        runtimeHeader_.setText("Runtime", juce::dontSendNotification);
        daw::ui::SettingsLookAndFeel::markSectionHeader(runtimeHeader_);
        addAndMakeVisible(runtimeHeader_);

        updateStatus();
    }

    juce::Component& getBadge() {
        return badge_;
    }

    void resized() override {
        rowLines_.clear();
        auto bounds = getLocalBounds();
        constexpr int rowH = 36;
        const auto takeRow = [this](juce::Rectangle<int>& b, int h) {
            auto row = b.removeFromTop(h);
            rowLines_.push_back(row.withTop(row.getBottom() - 1));
            return row;
        };
        const auto textWidth = [](juce::Button& button) {
            return juce::GlyphArrangement::getStringWidthInt(
                       FontManager::getInstance().getUIFont(13.5f), button.getButtonText()) +
                   32;
        };

        auto row = takeRow(bounds, rowH);
        browseButton_.setBounds(row.removeFromRight(textWidth(browseButton_)).reduced(0, 5));
        row.removeFromRight(8);
        modelLabel_.setBounds(row.removeFromLeft(120));
        modelEditor_.setBounds(row.reduced(0, 5));

        row = bounds.removeFromTop(46).reduced(0, 8);
        loadButton_.setBounds(row.removeFromLeft(textWidth(loadButton_) + 8));
        row.removeFromLeft(8);
        downloadButton_.setBounds(row.removeFromLeft(textWidth(downloadButton_)));
        row.removeFromLeft(12);
        statusLabel_.setBounds(row);

        bounds.removeFromTop(18);
        runtimeHeader_.setBounds(bounds.removeFromTop(30));

        row = takeRow(bounds, rowH);
        gpuCombo_.setBounds(row.removeFromRight(150).reduced(0, 5));
        if (gpuCustomEditor_.isVisible()) {
            row.removeFromRight(6);
            gpuCustomEditor_.setBounds(row.removeFromRight(56).reduced(0, 5));
        }
        gpuLabel_.setBounds(row);

        row = takeRow(bounds, rowH);
        tokensLabel_.setBounds(row.removeFromRight(46));
        ctxEditor_.setBounds(row.removeFromRight(80).reduced(0, 5));
        ctxLabel_.setBounds(row);

        loadOnStartupToggle_.setBounds(takeRow(bounds, rowH).reduced(0, 7));
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        for (const auto& line : rowLines_)
            g.fillRect(line);
    }

    void load(const Config& config) {
        modelEditor_.setText(juce::String(config.getLocalModelPath()), juce::dontSendNotification);
        setGpuLayers(config.getLocalLlamaGpuLayers());
        ctxEditor_.setText(juce::String(config.getLocalLlamaContextSize()),
                           juce::dontSendNotification);
        loadOnStartupToggle_.setToggleState(config.getLoadModelOnStartup(),
                                            juce::dontSendNotification);
        updateStatus();
    }

    void apply(Config& config) const {
        config.setLocalModelPath(modelEditor_.getText().toStdString());
        config.setLocalLlamaGpuLayers(getGpuLayers());
        config.setLocalLlamaContextSize(ctxEditor_.getText().getIntValue());
        config.setLoadModelOnStartup(loadOnStartupToggle_.getToggleState());
    }

    static bool isModelLoaded() {
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
                            downloadButton_.setButtonText(juce::String(
                                juce::CharPointer_UTF8("Download a model\xe2\x80\xa6")));

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
                std::string errorMessage;
                bool ok = LlamaModelManager::getInstance().loadModel(cfg, &errorMessage);
                juce::MessageManager::callAsync([this, ok, cfg, errorMessage]() {
                    loadButton_.setEnabled(true);
                    if (ok) {
                        auto& config = Config::getInstance();
                        config.setLocalModelPath(cfg.modelPath);
                        config.setLocalLlamaGpuLayers(cfg.gpuLayers);
                        config.setLocalLlamaContextSize(cfg.contextSize);
                        updateStatus();
                    } else {
                        statusLabel_.setText(errorMessage.empty()
                                                 ? juce::String("Failed to load model")
                                                 : juce::String(errorMessage),
                                             juce::dontSendNotification);
                        statusLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
                    }
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
            badge_.setStatus("Loaded: " + path, true);
        } else {
            loadButton_.setButtonText("Load model");
            badge_.setStatus("No model loaded", false);
        }
        statusLabel_.setText({}, juce::dontSendNotification);
        resized();
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
    juce::Label runtimeHeader_;
    juce::Label tokensLabel_;
    SettingsStatusBadge badge_;
    std::vector<juce::Rectangle<int>> rowLines_;
    juce::ToggleButton loadOnStartupToggle_;
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

class AISettingsPages::ConfigPage : public juce::Component {
  public:
    CloudPage* cloudPage = nullptr;
    LocalPage* localPage = nullptr;

    // One Advanced-grid row per agent role.
    struct AgentRow {
        std::string role;
        juce::Label nameLabel;
        juce::ComboBox providerCombo;
        juce::ComboBox modelCombo;
        std::vector<std::string> providerIds;  // parallels providerCombo items
    };

    ConfigPage() {
        // Setup selector: Simple (preset) / Advanced (per-agent grid)
        setupLabel_.setText("Setup", juce::dontSendNotification);
        styleLabel(setupLabel_);
        addAndMakeVisible(setupLabel_);

        setupCombo_.addItem("Simple", 1);
        setupCombo_.addItem("Advanced", 2);
        setupCombo_.setSelectedId(1, juce::dontSendNotification);
        styleCombo(setupCombo_);
        setupCombo_.onChange = [this]() {
            if (setupCombo_.getSelectedId() == 2)
                seedAdvancedFromSimple();
            updateSetupUI();
        };
        addAndMakeVisible(setupCombo_);

        // Mode selector: Local / Cloud / Hybrid
        modeLabel_.setText("Run on", juce::dontSendNotification);
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
        providerCombo_.onChange = [this] { refreshBadge(); };
        addAndMakeVisible(providerCombo_);

        // Optimize selector (cloud/hybrid)
        optimizeLabel_.setText("Optimize for", juce::dontSendNotification);
        styleLabel(optimizeLabel_);
        addAndMakeVisible(optimizeLabel_);
        styleCombo(optimizeCombo_);
        addAndMakeVisible(optimizeCombo_);

        // Local source selector: embedded GGUF vs OpenAI-compatible server.
        localSourceLabel_.setText("Source", juce::dontSendNotification);
        styleLabel(localSourceLabel_);
        addAndMakeVisible(localSourceLabel_);

        localSourceCombo_.addItem("Embedded", 1);
        localSourceCombo_.addItem("Local server", 2);
        localSourceCombo_.setSelectedId(1, juce::dontSendNotification);
        styleCombo(localSourceCombo_);
        localSourceCombo_.onChange = [this]() { updateModeUI(); };
        addAndMakeVisible(localSourceCombo_);

        // Local model name label (embedded GGUF source)
        modelNameLabel_.setText("No model loaded", juce::dontSendNotification);
        styleLabel(modelNameLabel_);
        modelNameLabel_.setColour(juce::Label::textColourId,
                                  ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        addAndMakeVisible(modelNameLabel_);

        // Local-server model picker (shown when Source = Local server). Lives
        // here so the model is chosen right where the source is selected.
        serverModelLabel_.setText("Model", juce::dontSendNotification);
        styleLabel(serverModelLabel_);
        addAndMakeVisible(serverModelLabel_);

        serverModelCombo_.setEditableText(true);  // allow manual id entry
        styleCombo(serverModelCombo_);
        addAndMakeVisible(serverModelCombo_);

        serverRefreshBtn_.setButtonText("Refresh");
        serverRefreshBtn_.onClick = [this]() { refreshServerModels(); };
        addAndMakeVisible(serverRefreshBtn_);

        styleLabel(serverStatusLabel_, 11.0f);
        serverStatusLabel_.setColour(juce::Label::textColourId,
                                     ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        addAndMakeVisible(serverStatusLabel_);

        modeCombo_.setSelectedId(1, juce::dontSendNotification);
        updateModeUI();

        modelHeader_.setText("Model", juce::dontSendNotification);
        daw::ui::SettingsLookAndFeel::markSectionHeader(modelHeader_);
        addAndMakeVisible(modelHeader_);
        for (auto* segments :
             {&setupSegments_, &modeSegments_, &optimizeSegments_, &sourceSegments_})
            addAndMakeVisible(*segments);

        mcpSectionLabel_.setText("Tools", juce::dontSendNotification);
        daw::ui::SettingsLookAndFeel::markSectionHeader(mcpSectionLabel_);
        addAndMakeVisible(mcpSectionLabel_);

        faustTitle_.setText("Faust DSP", juce::dontSendNotification);
        styleLabel(faustTitle_, 13.0f);
        addAndMakeVisible(faustTitle_);
        addAndMakeVisible(faustMcpToggle_);

        faustMcpHint_.setText(
            "Checks AI-written Faust code before loading it. Needs Node.js (npx).",
            juce::dontSendNotification);
        faustMcpHint_.setFont(FontManager::getInstance().getUIFont(11.5f));
        faustMcpHint_.setColour(juce::Label::textColourId,
                                ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        faustMcpHint_.setBorderSize({});
        addAndMakeVisible(faustMcpHint_);

        const bool npx = npxAvailable();
        npxStatus_.setText(npx ? "npx found" : "npx not found", juce::dontSendNotification);
        styleLabel(npxStatus_, 11.5f);
        npxStatus_.setJustificationType(juce::Justification::centredRight);
        npxStatus_.setColour(juce::Label::textColourId,
                             npx ? ActiveTheme::getColour(ActiveTheme::ACCENT_POSITIVE)
                                 : ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        addAndMakeVisible(npxStatus_);

        // One link rather than three sets of per-OS steps: the official
        // download page already offers the macOS, Windows and Linux installers
        // and stays current on its own.
        nodeDownloadLink_.setURL(juce::URL("https://nodejs.org/en/download"));
        nodeDownloadLink_.setButtonText("Get Node.js");
        nodeDownloadLink_.setFont(FontManager::getInstance().getUIFont(11.5f), false,
                                  juce::Justification::centredLeft);
        nodeDownloadLink_.setColour(juce::HyperlinkButton::textColourId,
                                    ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        addAndMakeVisible(nodeDownloadLink_);

        // Advanced per-agent grid: one row per agent role.
        advancedHintLabel_.setFont(FontManager::getInstance().getUIFont(11.5f));
        advancedHintLabel_.setColour(juce::Label::textColourId,
                                     ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        addAndMakeVisible(advancedHintLabel_);

        static constexpr std::array<std::pair<const char*, const char*>, 6> kRoles = {{
            {magda::role::COMMAND, "Command"},
            {magda::role::MUSIC, "Music"},
            {magda::role::FAUST, "Faust"},
            {magda::role::CHORD, "Chord"},
            {magda::role::CONTROLLER, "Controller"},
            {magda::role::THEME, "Theme"},
        }};
        advancedHintLabel_.setText("Each agent uses its own provider and model. Providers come "
                                   "from the Providers and Local models tabs.",
                                   juce::dontSendNotification);
        for (size_t i = 0; i < agentRows_.size(); ++i) {
            auto& r = agentRows_[i];
            r.role = kRoles[i].first;
            r.nameLabel.setText(kRoles[i].second, juce::dontSendNotification);
            styleLabel(r.nameLabel);
            addAndMakeVisible(r.nameLabel);

            styleCombo(r.providerCombo);
            AgentRow* rp = &r;
            r.providerCombo.onChange = [this, rp]() { fillRowModel(*rp, {}, {}); };
            addAndMakeVisible(r.providerCombo);

            // Pick-from-list only - no free-text model entry.
            styleCombo(r.modelCombo);
            addAndMakeVisible(r.modelCombo);
        }

        updateSetupUI();
    }

    juce::Component& getToolbar() {
        return badge_;
    }

    void resized() final {
        rowLines_.clear();
        auto bounds = getLocalBounds().reduced(0, 8);

        modelHeader_.setBounds(bounds.removeFromTop(kHeaderH));
        segmentRow(takeRow(bounds), setupLabel_, setupSegments_);
        setupCombo_.setBounds({});

        if (setupCombo_.getSelectedId() == 2)
            layoutAdvanced(bounds);
        else
            layoutSimple(bounds);

        bounds.removeFromTop(22);
        mcpSectionLabel_.setBounds(bounds.removeFromTop(kHeaderH));
        auto row = takeRow(bounds, 52);
        faustMcpToggle_.setBounds(row.removeFromRight(36).withSizeKeepingCentre(36, 22));
        row.removeFromRight(10);
        npxStatus_.setBounds(row.removeFromRight(110));
        faustTitle_.setBounds(row.removeFromTop(26).withTrimmedTop(6));
        const int hintW = juce::GlyphArrangement::getStringWidthInt(faustMcpHint_.getFont(),
                                                                    faustMcpHint_.getText()) +
                          6;
        auto hintRow = row.removeFromTop(18);
        faustMcpHint_.setBounds(hintRow.removeFromLeft(juce::jmin(hintW, hintRow.getWidth() - 80)));
        nodeDownloadLink_.setBounds(hintRow.removeFromLeft(80));
        refreshBadge();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        for (const auto& line : rowLines_)
            g.fillRect(line);
    }

    void layoutSimple(juce::Rectangle<int>& bounds) {
        segmentRow(takeRow(bounds), modeLabel_, modeSegments_);
        modeCombo_.setBounds({});

        if (modeCombo_.getSelectedId() == 1) {
            // Local: source, then either the embedded GGUF or the server's model picker.
            segmentRow(takeRow(bounds), localSourceLabel_, sourceSegments_);
            localSourceCombo_.setBounds({});
            if (localSourceCombo_.getSelectedId() == 2) {
                auto row = takeRow(bounds);
                serverRefreshBtn_.setBounds(row.removeFromRight(80).reduced(0, 5));
                row.removeFromRight(6);
                serverModelCombo_.setBounds(
                    row.removeFromRight(juce::jlimit(150, 240, row.getWidth() / 2)).reduced(0, 5));
                serverModelLabel_.setBounds(row);
                serverStatusLabel_.setBounds(bounds.removeFromTop(20));
            } else {
                auto row = takeRow(bounds);
                modelNameLabel_.setJustificationType(juce::Justification::centredRight);
                modelNameLabel_.setBounds(row.removeFromRight(row.getWidth() / 2));
                serverModelLabel_.setBounds({});
            }
        } else {
            auto row = takeRow(bounds);
            providerCombo_.setBounds(
                row.removeFromRight(juce::jlimit(150, 220, row.getWidth() / 2)).reduced(0, 5));
            providerLabel_.setBounds(row);
            segmentRow(takeRow(bounds), optimizeLabel_, optimizeSegments_);
            optimizeCombo_.setBounds({});
        }
    }

    void layoutAdvanced(juce::Rectangle<int>& bounds) {
        constexpr int nameW = 100;
        constexpr int providerW = 160;

        advancedHintLabel_.setBounds(bounds.removeFromTop(28));
        for (auto& r : agentRows_) {
            auto row = takeRow(bounds);
            r.nameLabel.setBounds(row.removeFromLeft(nameW));
            r.providerCombo.setBounds(row.removeFromLeft(providerW).reduced(0, 5));
            row.removeFromLeft(8);
            r.modelCombo.setBounds(row.reduced(0, 5));
        }
    }

    static constexpr int kHeaderH = 30;
    static constexpr int kRowH = 36;

    juce::Rectangle<int> takeRow(juce::Rectangle<int>& bounds, int height = kRowH) {
        auto row = bounds.removeFromTop(height);
        rowLines_.push_back(row.withTop(row.getBottom() - 1));
        return row;
    }

    static void segmentRow(juce::Rectangle<int> row, juce::Label& label, ComboSegments& segments) {
        segments.sync();
        const int w = juce::jmin(segments.getIdealWidth(), row.getWidth() / 2 + 40);
        segments.setBounds(row.removeFromRight(w).reduced(0, 6));
        label.setBounds(row);
    }

    // What the assistant runs on now, at the right of the tab row.
    void refreshBadge() {
        if (setupCombo_.getSelectedId() == 2) {
            badge_.setStatus("Advanced", true);
            return;
        }
        switch (modeCombo_.getSelectedId()) {
            case 1:
                badge_.setStatus(localSourceCombo_.getSelectedId() == 2 ? "Local server" : "Local",
                                 true);
                break;
            case 3:
                badge_.setStatus(providerCombo_.getText() + " \xc2\xb7 hybrid",
                                 providerCombo_.getNumItems() > 0);
                break;
            default:
                badge_.setStatus(providerCombo_.getNumItems() > 0
                                     ? providerCombo_.getText() + " \xc2\xb7 cloud"
                                     : juce::String("No provider"),
                                 providerCombo_.getNumItems() > 0);
                break;
        }
    }

    void refreshProviderCombos() {
        providerCombo_.clear();
        int nextId = 1;
        if (cloudPage) {
            for (const auto& pid : cloudPage->getConfiguredProviders()) {
                const auto* info = findProviderInfo(pid);
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

    // Provider options offered in the Advanced grid: configured cloud
    // providers (from the Cloud tab) plus one Local group. The right-hand
    // combo chooses the specific local backend.
    std::vector<std::pair<std::string, juce::String>> advancedProviderOptions() const {
        std::vector<std::pair<std::string, juce::String>> opts;
        if (cloudPage) {
            for (const auto& pid : cloudPage->getConfiguredProviders())
                if (const auto* info = findProviderInfo(pid))
                    opts.emplace_back(pid, juce::String(info->displayName));
        }
        opts.emplace_back(magda::provider::LLAMA_LOCAL, juce::String("Local"));
        return opts;
    }

    static bool isLocalProviderId(const std::string& id) {
        return id == magda::provider::LLAMA_LOCAL || id == magda::provider::LOCAL_SERVER ||
               id == magda::provider::FAST_INFERENCE;
    }

    static std::string rowProviderGroupId(const AgentRow& row) {
        int idx = row.providerCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(row.providerIds.size()))
            return row.providerIds[static_cast<size_t>(idx)];
        return {};
    }

    std::string rowProviderId(const AgentRow& row) const {
        const auto groupId = rowProviderGroupId(row);
        if (groupId != magda::provider::LLAMA_LOCAL)
            return groupId;

        if (row.modelCombo.getSelectedId() == 2)
            return magda::provider::LOCAL_SERVER;
        if (row.role == magda::role::COMMAND && row.modelCombo.getSelectedId() == 3)
            return magda::provider::FAST_INFERENCE;
        return magda::provider::LLAMA_LOCAL;
    }

    // Select the combo item matching a provider id. openai_responses and
    // openai_chat share one "OpenAI" entry, and all local backends share one
    // "Local" entry.
    static void selectRowProviderById(AgentRow& row, std::string id) {
        if (id == magda::provider::OPENAI_RESPONSES)
            id = magda::provider::OPENAI_CHAT;
        if (isLocalProviderId(id))
            id = magda::provider::LLAMA_LOCAL;
        for (size_t i = 0; i < row.providerIds.size(); ++i) {
            auto cand = row.providerIds[i];
            if (cand == magda::provider::OPENAI_RESPONSES)
                cand = magda::provider::OPENAI_CHAT;
            if (cand == id) {
                row.providerCombo.setSelectedId(static_cast<int>(i) + 1,
                                                juce::dontSendNotification);
                return;
            }
        }
        if (!row.providerIds.empty())
            row.providerCombo.setSelectedId(1, juce::dontSendNotification);
    }

    // Fill the right-hand combo with either a cloud model catalogue or the
    // available local backends. desiredProvider restores a saved local choice.
    void fillRowModel(AgentRow& row, const juce::String& desiredModel,
                      const std::string& desiredProvider) {
        const auto providerGroupId = rowProviderGroupId(row);
        row.modelCombo.clear(juce::dontSendNotification);
        if (providerGroupId == magda::provider::LLAMA_LOCAL) {
            row.modelCombo.addItem("Embedded", 1);
            row.modelCombo.addItem("Server", 2);
            if (row.role == magda::role::COMMAND)
                row.modelCombo.addItem("Fast Inference (Command)", 3);

            int selectedId = 1;
            if (desiredProvider == magda::provider::LOCAL_SERVER)
                selectedId = 2;
            else if (row.role == magda::role::COMMAND &&
                     desiredProvider == magda::provider::FAST_INFERENCE)
                selectedId = 3;
            row.modelCombo.setSelectedId(selectedId, juce::dontSendNotification);
            row.modelCombo.setEnabled(true);
            return;
        }
        row.modelCombo.setEnabled(true);

        auto models = knownModelsForProvider(providerGroupId);
        bool desiredInCatalogue = false;
        for (const auto& m : models)
            if (m == desiredModel) {
                desiredInCatalogue = true;
                break;
            }

        int id = 1;
        int selectId = 0;
        // Keep any saved model selectable even if it predates the catalogue.
        if (desiredModel.isNotEmpty() && !desiredInCatalogue) {
            row.modelCombo.addItem(desiredModel, id);
            selectId = id;
            ++id;
        }
        for (const auto& m : models) {
            row.modelCombo.addItem(m, id);
            if (m == desiredModel)
                selectId = id;
            ++id;
        }
        row.modelCombo.setSelectedId(selectId > 0 ? selectId : 1, juce::dontSendNotification);
    }

    // Rebuild every row's provider combo from the current configured
    // providers, preserving each row's provider + model selection.
    void refreshAgentProviderCombos() {
        auto opts = advancedProviderOptions();
        for (auto& row : agentRows_) {
            auto prevProvider = rowProviderId(row);
            auto prevModel = row.modelCombo.getText();
            row.providerCombo.clear(juce::dontSendNotification);
            row.providerIds.clear();
            int itemId = 1;
            for (const auto& [pid, disp] : opts) {
                row.providerCombo.addItem(disp, itemId++);
                row.providerIds.push_back(pid);
            }
            selectRowProviderById(row, prevProvider);
            fillRowModel(row, prevModel, prevProvider);
        }
    }

    // Seed the Advanced grid from whatever the Simple UI currently resolves to,
    // so switching to Advanced starts from a known baseline.
    void seedAdvancedFromSimple() {
        refreshAgentProviderCombos();
        std::string presetId;
        auto cfgs = computeSimpleConfigs(presetId);
        for (auto& row : agentRows_) {
            auto it = cfgs.find(row.role);
            if (it == cfgs.end())
                continue;
            selectRowProviderById(row, it->second.provider);
            fillRowModel(row, juce::String(it->second.model), it->second.provider);
        }
    }

    // Toggle Simple vs Advanced control visibility.
    void updateSetupUI() {
        const bool advanced = (setupCombo_.getSelectedId() == 2);

        modeLabel_.setVisible(!advanced);
        modeCombo_.setVisible(!advanced);
        if (advanced) {
            providerLabel_.setVisible(false);
            providerCombo_.setVisible(false);
            optimizeLabel_.setVisible(false);
            optimizeCombo_.setVisible(false);
            localSourceLabel_.setVisible(false);
            localSourceCombo_.setVisible(false);
            modelNameLabel_.setVisible(false);
            serverModelLabel_.setVisible(false);
            serverModelCombo_.setVisible(false);
            serverRefreshBtn_.setVisible(false);
            serverStatusLabel_.setVisible(false);
        }

        advancedHintLabel_.setVisible(advanced);
        for (auto& row : agentRows_) {
            row.nameLabel.setVisible(advanced);
            row.providerCombo.setVisible(advanced);
            row.modelCombo.setVisible(advanced);
        }

        if (!advanced)
            updateModeUI();  // restores Simple sub-visibility and calls resized()
        else
            resized();
    }

    // Called when the Config tab becomes visible: re-pull provider combos from
    // the Cloud page and refresh the local-server summary from the Local page's
    // live selection.
    void refreshOnShow() {
        refreshProviderCombos();
        refreshAgentProviderCombos();
        if (setupCombo_.getSelectedId() != 2 && modeCombo_.getSelectedId() == 1)
            updateLocalModelLabel();
    }

    void load(Config& config) {
        auto presetId = config.getAIPreset();

        // MCP toggle
        auto mcpServers = config.getMCPServers();
        bool faustMcpEnabled = false;
        for (const auto& srv : mcpServers) {
            if (srv.name == "faust-mcp" && srv.enabled)
                faustMcpEnabled = true;
        }
        faustMcpToggle_.setToggleState(faustMcpEnabled, juce::dontSendNotification);

        const bool advanced = (presetId == magda::preset::ADVANCED);
        setupCombo_.setSelectedId(advanced ? 2 : 1, juce::dontSendNotification);

        // Determine mode from preset (also seeds the Simple baseline used if the
        // user switches back from Advanced; an "advanced" preset falls through
        // to the Cloud branch below).
        if (presetId.starts_with("local") || presetId == magda::preset::LOCAL_EMBEDDED) {
            modeCombo_.setSelectedId(1, juce::dontSendNotification);
            localSourceCombo_.setSelectedId(presetId == magda::preset::LOCAL_SERVER ? 2 : 1,
                                            juce::dontSendNotification);
        } else if (presetId.starts_with("hybrid")) {
            modeCombo_.setSelectedId(3, juce::dontSendNotification);
            if (presetId == magda::preset::HYBRID_SPEED)
                savedOptimize_ = "Speed";
            else
                savedOptimize_ = "Quality";
        } else {
            modeCombo_.setSelectedId(2, juce::dontSendNotification);
            // Infer optimize from whether command uses cloud
            auto cmdCfg = config.getAgentLLMConfig(magda::role::COMMAND);
            auto musicCfg = config.getAgentLLMConfig(magda::role::MUSIC);
            if (cmdCfg.provider == musicCfg.provider && cmdCfg.model == musicCfg.model)
                savedOptimize_ = "Quality";
            else
                savedOptimize_ = "Cost";
        }

        // Determine provider from music agent config
        auto musicCfg = config.getAgentLLMConfig(magda::role::MUSIC);
        if (musicCfg.provider == magda::provider::ANTHROPIC)
            savedProviderDisplay_ = "Anthropic";
        else if (musicCfg.provider == magda::provider::GEMINI)
            savedProviderDisplay_ = "Gemini";
        else if (musicCfg.provider == magda::provider::DEEPSEEK)
            savedProviderDisplay_ = "DeepSeek";
        else if (musicCfg.provider == magda::provider::OPENROUTER)
            savedProviderDisplay_ = "OpenRouter";
        else if (musicCfg.provider == magda::provider::OPENAI_CHAT ||
                 musicCfg.provider == magda::provider::OPENAI_RESPONSES)
            savedProviderDisplay_ = "OpenAI";

        // Seed the local-server model combo from saved config.
        serverModelCombo_.clear(juce::dontSendNotification);
        serverModelCombo_.setText(juce::String(config.getLocalServerModel()),
                                  juce::dontSendNotification);

        if (advanced)
            loadAdvanced(config);

        updateLocalModelLabel();
        updateSetupUI();
    }

    // Seed the Advanced grid from the persisted per-agent configs.
    void loadAdvanced(Config& config) {
        refreshAgentProviderCombos();
        for (auto& row : agentRows_) {
            auto cfg = config.getAgentLLMConfig(row.role);
            selectRowProviderById(row, cfg.provider);
            fillRowModel(row, juce::String(cfg.model), cfg.provider);
        }
    }

    // Embedded-GGUF summary (the local-server source uses the model combo, not
    // this label).
    void updateLocalModelLabel() {
        auto modelPath = Config::getInstance().getLocalModelPath();
        if (!modelPath.empty())
            modelNameLabel_.setText(juce::File(juce::String(modelPath)).getFileName(),
                                    juce::dontSendNotification);
        else
            modelNameLabel_.setText("No model configured", juce::dontSendNotification);
    }

    // Probe GET /v1/models for the configured local server and fill the model
    // combo. Uses the Local tab's live URL/key if present, else saved config.
    void refreshServerModels() {
        auto& config = Config::getInstance();
        juce::String url = juce::String(config.getLocalServerUrl());
        juce::String key = juce::String(config.getLocalServerApiKey());
        auto current = serverModelCombo_.getText();

        serverRefreshBtn_.setEnabled(false);
        serverStatusLabel_.setText("Loading models...", juce::dontSendNotification);
        serverStatusLabel_.setColour(juce::Label::textColourId,
                                     ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));

        auto safeThis = juce::Component::SafePointer<ConfigPage>(this);
        juce::Thread::launch([safeThis, url, key, current]() {
            auto probe = probeLocalServerModels(url, key);
            juce::MessageManager::callAsync([safeThis, probe, current]() {
                if (!safeThis)
                    return;
                safeThis->serverRefreshBtn_.setEnabled(true);
                if (!probe.ok) {
                    safeThis->serverStatusLabel_.setText(probe.error, juce::dontSendNotification);
                    safeThis->serverStatusLabel_.setColour(juce::Label::textColourId,
                                                           juce::Colours::orange);
                    return;
                }
                safeThis->serverModelCombo_.clear(juce::dontSendNotification);
                int id = 1;
                for (const auto& m : probe.models)
                    safeThis->serverModelCombo_.addItem(m, id++);
                // Keep a prior pick; else auto-select the first so a single
                // Refresh fully configures a one-model server.
                if (current.isNotEmpty())
                    safeThis->serverModelCombo_.setText(current, juce::dontSendNotification);
                else if (!probe.models.empty())
                    safeThis->serverModelCombo_.setSelectedId(1, juce::dontSendNotification);
                safeThis->serverStatusLabel_.setText(
                    juce::String(static_cast<int>(probe.models.size())) + " models available",
                    juce::dontSendNotification);
                safeThis->serverStatusLabel_.setColour(juce::Label::textColourId,
                                                       juce::Colours::limegreen);
            });
        });
    }

    void apply(Config& config) {
        applyMcp(config);
        if (setupCombo_.getSelectedId() == 2)
            applyAdvanced(config);
        else
            applySimple(config);
    }

  private:
    void applyMcp(Config& config) {
        auto mcpServers = config.getMCPServers();
        bool wantFaustMcp = faustMcpToggle_.getToggleState();

        // Find or create the faust-mcp entry
        bool found = false;
        for (auto& srv : mcpServers) {
            if (srv.name == "faust-mcp") {
                srv.enabled = wantFaustMcp;
                found = true;
                break;
            }
        }
        if (!found && wantFaustMcp) {
            Config::MCPServerConfig srv;
            srv.name = "faust-mcp";
            srv.command = "npx";
            srv.args = {"faust-mcp-magda"};
            srv.enabled = true;
            mcpServers.push_back(std::move(srv));
        }
        config.setMCPServers(mcpServers);
    }

    void applySimple(Config& config) {
        std::string presetId;
        auto cfgs = computeSimpleConfigs(presetId);
        config.setAIPreset(presetId);
        for (const auto& [role, cfg] : cfgs)
            config.setAgentLLMConfig(role, cfg);
        // Persist the chosen local-server model (this tab owns it).
        if (modeCombo_.getSelectedId() == 1 && localSourceCombo_.getSelectedId() == 2)
            config.setLocalServerModel(serverModelCombo_.getText().trim().toStdString());
    }

    void applyAdvanced(Config& config) {
        for (auto& row : agentRows_) {
            Config::AgentLLMConfig cfg;
            auto pid = rowProviderId(row);
            auto model = row.modelCombo.getText().trim();
            // OpenAI: gpt-5* and the o-series require the Responses API
            // provider; gpt-4.1* use the Chat provider.
            if (pid == magda::provider::OPENAI_CHAT && magda::requiresOpenAIResponsesAPI(model))
                pid = magda::provider::OPENAI_RESPONSES;
            cfg.provider = pid;
            // Local providers carry no per-agent model - it resolves from the
            // loaded GGUF / shared local-server config at request time.
            const bool isLocal =
                (pid == magda::provider::LLAMA_LOCAL || pid == magda::provider::LOCAL_SERVER ||
                 pid == magda::provider::FAST_INFERENCE);
            cfg.model = isLocal ? std::string{} : model.toStdString();
            // apiKey left empty - resolved from Cloud-tab credentials at request
            // time (per-agent key first, then per-provider credential).
            config.setAgentLLMConfig(row.role, cfg);
        }
        config.setAIPreset(magda::preset::ADVANCED);
    }

    // Resolve the Simple UI selections into per-agent configs without writing to
    // Config. Returns the preset id via outPresetId. Shared by applySimple and
    // by seedAdvancedFromSimple.
    std::map<std::string, Config::AgentLLMConfig> computeSimpleConfigs(std::string& outPresetId) {
        std::map<std::string, Config::AgentLLMConfig> out;
        int mode = modeCombo_.getSelectedId();
        auto presetId = providerDisplayToPresetId(providerCombo_.getText());
        auto optimize = optimizeCombo_.getText();

        if (mode == 1) {
            // Local: embedded GGUF or OpenAI-compatible server.
            outPresetId = (localSourceCombo_.getSelectedId() == 2) ? magda::preset::LOCAL_SERVER
                                                                   : magda::preset::LOCAL_EMBEDDED;
            if (const auto* preset = magda::findPreset(outPresetId))
                for (const auto& [role, cfg] : preset->agents)
                    out[role] = cfg;
        } else if (mode == 2) {
            // Cloud
            outPresetId = presetId;
            if (const auto* preset = magda::findPreset(presetId)) {
                for (const auto& [role, presetCfg] : preset->agents) {
                    auto cfg = presetCfg;
                    cfg.apiKey = "";
                    out[role] = cfg;
                }
            }
        } else {
            // Hybrid: music + controller are cloud; command is cloud for speed
            // and local for cost.
            //
            // NB this branch builds the configs itself rather than reading the
            // HYBRID_* preset tables, so llm_presets.cpp and the code here have
            // to be kept in step — changing only the table silently does
            // nothing to the Simple UI.
            outPresetId =
                optimize == "Speed" ? magda::preset::HYBRID_SPEED : magda::preset::HYBRID_QUALITY;

            out[magda::role::MUSIC] = makeCloudConfig(magda::role::MUSIC, presetId);
            out[magda::role::CONTROLLER] = makeCloudConfig(magda::role::CONTROLLER, presetId);
            if (optimize == "Speed") {
                // Command stays cloud even here: the on-device command model is
                // brittle outside its template distribution (#1847), so it is
                // opt-in via Advanced rather than something a preset selects.
                out[magda::role::COMMAND] = makeCloudConfig(magda::role::COMMAND, presetId);
            } else {
                Config::AgentLLMConfig cmdLocal;
                cmdLocal.provider = magda::provider::LLAMA_LOCAL;
                out[magda::role::COMMAND] = cmdLocal;
            }
        }
        // Any role omitted by a hand-authored/simple preset inherits the music
        // tier. Local-server presets intentionally keep one shared model.
        if (auto it = out.find(magda::role::MUSIC); it != out.end())
            for (const auto* inheritedRole :
                 {magda::role::FAUST, magda::role::CHORD, magda::role::THEME})
                if (out.find(inheritedRole) == out.end())
                    out[inheritedRole] = it->second;
        return out;
    }

    void updateModeUI() {
        int mode = modeCombo_.getSelectedId();
        bool isLocal = (mode == 1);
        bool isCloud = (mode == 2);
        bool isHybrid = (mode == 3);

        const bool isServer = isLocal && localSourceCombo_.getSelectedId() == 2;
        localSourceLabel_.setVisible(isLocal);
        localSourceCombo_.setVisible(isLocal);
        // Embedded source → GGUF summary label; server source → model picker.
        modelNameLabel_.setVisible(isLocal && !isServer);
        serverModelLabel_.setVisible(isServer);
        serverModelCombo_.setVisible(isServer);
        serverRefreshBtn_.setVisible(isServer);
        serverStatusLabel_.setVisible(isServer);
        if (isLocal && !isServer)
            updateLocalModelLabel();
        if (isServer && serverModelCombo_.getNumItems() == 0)
            refreshServerModels();  // auto-discover on entering server source
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

    static std::string providerDisplayToPresetId(const juce::String& display) {
        if (display == "Anthropic")
            return magda::preset::CLOUD_ANTHROPIC;
        if (display == "Gemini")
            return magda::preset::CLOUD_GEMINI;
        if (display == "DeepSeek")
            return magda::preset::CLOUD_DEEPSEEK;
        if (display == "OpenRouter")
            return magda::preset::CLOUD_OPENROUTER;
        return magda::preset::CLOUD_OPENAI;
    }

    static Config::AgentLLMConfig makeCloudConfig(const std::string& role,
                                                  const std::string& presetId) {
        if (const auto* preset = magda::findPreset(presetId)) {
            auto it = preset->agents.find(role);
            if (it != preset->agents.end()) {
                auto cfg = it->second;
                cfg.apiKey = "";
                return cfg;
            }
        }
        // Fallback — should not happen with valid preset IDs
        return Config::AgentLLMConfig{};
    }

    // Setup selector: Simple (preset) vs Advanced (per-agent grid)
    juce::Label setupLabel_;
    juce::ComboBox setupCombo_;

    juce::Label modeLabel_;
    juce::ComboBox modeCombo_;
    juce::Label providerLabel_;
    juce::ComboBox providerCombo_;
    juce::Label optimizeLabel_;
    juce::ComboBox optimizeCombo_;
    juce::Label localSourceLabel_;
    juce::ComboBox localSourceCombo_;
    juce::Label modelNameLabel_;
    // Local-server model picker (Source = Local server)
    juce::Label serverModelLabel_, serverStatusLabel_;
    juce::ComboBox serverModelCombo_;
    juce::TextButton serverRefreshBtn_;
    juce::String savedProviderDisplay_;
    juce::String savedOptimize_ = "Quality";

    // Advanced per-agent grid (AgentRow defined near the top of the class).
    juce::Label advancedHintLabel_;
    std::array<AgentRow, 6> agentRows_;

    // MCP Tools
    juce::Label mcpSectionLabel_;
    juce::Label faustTitle_;
    juce::Label npxStatus_;
    juce::ToggleButton faustMcpToggle_;
    juce::Label faustMcpHint_;
    juce::HyperlinkButton nodeDownloadLink_;

    juce::Label modelHeader_;
    ComboSegments setupSegments_{setupCombo_};
    ComboSegments modeSegments_{modeCombo_};
    ComboSegments optimizeSegments_{optimizeCombo_};
    ComboSegments sourceSegments_{localSourceCombo_};
    SettingsStatusBadge badge_;
    std::vector<juce::Rectangle<int>> rowLines_;
};

// ============================================================================
// SampleTaggerPage — manage the CLAP audio/text model + RoBERTa tokenizer
// that power the media DB's semantic search (issue #768).
// ============================================================================

class AISettingsPages::SampleTaggerPage : public juce::Component {
  public:
    SampleTaggerPage() {
        statusLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        statusLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
        statusLabel_.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(statusLabel_);

        locationCaption_.setText("Models location", juce::dontSendNotification);
        styleLabel(locationCaption_);
        addAndMakeVisible(locationCaption_);

        // Read-only — the only sanctioned way to change it is the
        // Browse button, which validates the directory exists.
        locationField_.setReadOnly(true);
        styleEditor(locationField_, "");
        addAndMakeVisible(locationField_);

        browseButton_.setButtonText("Browse...");
        browseButton_.onClick = [this]() { browseForLocation(); };
        addAndMakeVisible(browseButton_);

        resetLocationButton_.setButtonText("Reset");
        resetLocationButton_.onClick = [this]() {
            magda::Config::getInstance().setSampleTaggerModelsDir(std::string{});
            magda::Config::getInstance().save();
            refreshStatus();
        };
        addAndMakeVisible(resetLocationButton_);

        progressBar_.setColour(juce::ProgressBar::backgroundColourId,
                               ActiveTheme::getColour(ActiveTheme::BACKGROUND).brighter(0.05f));
        progressBar_.setColour(juce::ProgressBar::foregroundColourId,
                               ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        progressBar_.setPercentageDisplay(false);
        progressBar_.setVisible(false);
        addAndMakeVisible(progressBar_);

        actionButton_.setButtonText("Download Sample Analyzer");
        actionButton_.onClick = [this]() { handleActionClick(); };
        addAndMakeVisible(actionButton_);

        // Load button — forces the lazy ORT sessions / tokenizer into
        // memory now, on a background thread so the dialog stays fluid.
        // Only enabled when the bundle is installed.
        loadButton_.setButtonText("Load");
        loadButton_.onClick = [this]() { handleLoadClick(); };
        addAndMakeVisible(loadButton_);

        // Load-at-startup toggle. When on, app startup kicks off a
        // background preloadModels() so the first text query doesn't pay
        // the ~5s load cost.
        loadOnStartupToggle_.setButtonText("Load on startup");
        loadOnStartupToggle_.setColour(juce::ToggleButton::textColourId,
                                       ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        addAndMakeVisible(loadOnStartupToggle_);

        refreshStatus();
    }

    void resized() final {
        auto bounds = getLocalBounds().reduced(12);
        const int rowH = 24;
        const int labelW = 110;

        statusLabel_.setBounds(bounds.removeFromTop(60));
        bounds.removeFromTop(10);

        auto locRow = bounds.removeFromTop(rowH);
        locationCaption_.setBounds(locRow.removeFromLeft(labelW));
        resetLocationButton_.setBounds(locRow.removeFromRight(70).reduced(0, 1));
        locRow.removeFromRight(4);
        browseButton_.setBounds(locRow.removeFromRight(90).reduced(0, 1));
        locRow.removeFromRight(4);
        locationField_.setBounds(locRow.reduced(0, 1));
        bounds.removeFromTop(10);

        progressBar_.setBounds(bounds.removeFromTop(22));
        bounds.removeFromTop(8);

        auto buttonRow = bounds.removeFromTop(28);
        actionButton_.setBounds(buttonRow.removeFromLeft(220));
        buttonRow.removeFromLeft(8);
        loadButton_.setBounds(buttonRow.removeFromLeft(100));
        bounds.removeFromTop(10);
        loadOnStartupToggle_.setBounds(bounds.removeFromTop(22));
    }

    void load(const magda::Config& config) {
        loadOnStartupToggle_.setToggleState(config.getLoadSampleTaggerOnStartup(),
                                            juce::dontSendNotification);
        refreshStatus();
    }
    // The models folder persists on Browse/Reset: downloads go there at once.
    void apply(magda::Config& config) const {
        config.setLoadSampleTaggerOnStartup(loadOnStartupToggle_.getToggleState());
    }

  private:
    void refreshStatus() {
        const auto currentDir =
            juce::String(magda::media::MediaDbContext::getInstance().modelsDir().string());
        locationField_.setText(currentDir, juce::dontSendNotification);

        const bool installed = magda::media::MediaModelDownloader::isInstalled(
            magda::media::MediaModelDownloader::Bundle::SampleTagger);
        auto& ctx = magda::media::MediaDbContext::getInstance();
        const bool loaded =
            ctx.isAudioEncoderLoaded() && ctx.isTextEncoderLoaded() && ctx.isTokenizerLoaded();

        if (installed) {
            statusLabel_.setText(
                juce::String("Sample Analyzer is installed (") +
                    (loaded ? "loaded in memory" : "not loaded - first query will load it") +
                    ").\n\nThis enables text search ('warm pad', 'kick 808'...) over your indexed "
                    "sample library. Click Remove to free disk space if you don't use text "
                    "search.",
                juce::dontSendNotification);
            actionButton_.setButtonText("Remove");
            progressBar_.setVisible(false);
        } else {
            const auto totalMb = magda::media::MediaModelDownloader::expectedTotalBytes(
                                     magda::media::MediaModelDownloader::Bundle::SampleTagger) /
                                 (1024.0 * 1024.0);
            statusLabel_.setText(
                "Sample Analyzer is not installed.\n\nDownload (~" + juce::String(totalMb, 0) +
                    " MB) to enable text search over indexed samples. Without it, the media "
                    "library still supports filename / tag / family filtering.",
                juce::dontSendNotification);
            actionButton_.setButtonText("Download Sample Analyzer");
            progressBar_.setVisible(false);
        }
        actionButton_.setEnabled(true);
        loadButton_.setEnabled(installed && !loaded && !loadInFlight_);
        loadButton_.setButtonText(loaded ? "Unload" : (loadInFlight_ ? "Loading..." : "Load"));
        loadButton_.setEnabled(installed && !loadInFlight_);
        resized();
    }

    void handleLoadClick() {
        auto& ctx = magda::media::MediaDbContext::getInstance();
        const bool loaded =
            ctx.isAudioEncoderLoaded() && ctx.isTextEncoderLoaded() && ctx.isTokenizerLoaded();
        if (loaded) {
            ctx.unloadModels();
            refreshStatus();
            return;
        }
        // Async preload — ORT Session construction is multi-second.
        loadInFlight_ = true;
        refreshStatus();
        const juce::Component::SafePointer<SampleTaggerPage> self(this);
        juce::Thread::launch([self]() {
            magda::media::MediaDbContext::getInstance().preloadModels();
            juce::MessageManager::callAsync([self]() {
                if (self != nullptr) {
                    self->loadInFlight_ = false;
                    self->refreshStatus();
                }
            });
        });
    }

    void browseForLocation() {
        const auto currentDir = juce::File(
            juce::String(magda::media::MediaDbContext::getInstance().modelsDir().string()));
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder for the Sample Analyzer models",
            currentDir.exists() ? currentDir
                                : juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [this](const juce::FileChooser& fc) {
                                      const auto picked = fc.getResult();
                                      if (!picked.isDirectory()) {
                                          return;  // user cancelled
                                      }
                                      magda::Config::getInstance().setSampleTaggerModelsDir(
                                          picked.getFullPathName().toStdString());
                                      magda::Config::getInstance().save();
                                      refreshStatus();
                                  });
    }

    void handleActionClick() {
        if (downloader_.isRunning()) {
            downloader_.cancel();
            return;
        }
        if (magda::media::MediaModelDownloader::isInstalled(
                magda::media::MediaModelDownloader::Bundle::SampleTagger)) {
            removeInstalledFiles();
            refreshStatus();
            return;
        }
        // Start download
        progressBar_.setVisible(true);
        progressValue_ = 0.0;
        actionButton_.setButtonText("Cancel");
        statusLabel_.setText("Starting download...", juce::dontSendNotification);

        const juce::Component::SafePointer<SampleTaggerPage> self(this);
        downloader_.start([self](const auto& p) {
            if (self != nullptr) {
                self->onProgress(p);
            }
        });
    }

    void onProgress(const magda::media::MediaModelDownloader::Progress& p) {
        using Phase = magda::media::MediaModelDownloader::Phase;
        switch (p.phase) {
            case Phase::Downloading:
            case Phase::Verifying: {
                const auto total = p.totalBytesAll > 0 ? p.totalBytesAll : 1;
                progressValue_ = static_cast<double>(p.bytesDoneAll) / static_cast<double>(total);
                const auto mb = [](juce::int64 b) {
                    return juce::String(b / (1024.0 * 1024.0), 1);
                };
                const auto verb = (p.phase == Phase::Verifying ? juce::String("Verifying ")
                                                               : juce::String("Downloading "));
                statusLabel_.setText(verb + p.currentFilename + "  (" + mb(p.bytesDoneAll) + " / " +
                                         mb(p.totalBytesAll) + " MB)",
                                     juce::dontSendNotification);
                progressBar_.repaint();
                break;
            }
            case Phase::Done:
                refreshStatus();
                break;
            case Phase::Failed:
                actionButton_.setButtonText("Retry");
                statusLabel_.setText(juce::String("Download failed: ") + p.errorMessage,
                                     juce::dontSendNotification);
                progressBar_.setVisible(false);
                break;
            case Phase::Cancelled:
                refreshStatus();
                break;
            case Phase::Idle:
                break;
        }
    }

    static void removeInstalledFiles() {
        magda::media::MediaModelDownloader::remove(
            magda::media::MediaModelDownloader::Bundle::SampleTagger);
    }

    juce::Label statusLabel_;
    juce::Label locationCaption_;
    juce::TextEditor locationField_;
    juce::TextButton browseButton_;
    juce::TextButton resetLocationButton_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    // ProgressBar holds a reference to the value, so the value must come
    // first in the member-decl order to be constructed first.
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_{progressValue_};
    juce::TextButton actionButton_;
    juce::TextButton loadButton_;
    juce::ToggleButton loadOnStartupToggle_;
    bool loadInFlight_ = false;
    magda::media::MediaModelDownloader downloader_{
        magda::media::MediaModelDownloader::Bundle::SampleTagger};
};

// ============================================================================
// BeatTrackerPage — the model behind the measured BPM tier (issue #2674)
// ============================================================================

class AISettingsPages::BeatTrackerPage : public juce::Component {
  public:
    using Bundle = magda::media::MediaModelDownloader::Bundle;

    BeatTrackerPage() {
        statusLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        statusLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
        statusLabel_.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(statusLabel_);

        sourceLink_.setButtonText("huggingface.co/ConceptualMachines/magda-beat-tracker");
        sourceLink_.setURL(
            juce::URL(magda::media::MediaModelDownloader::sourceUrl(Bundle::BeatTracker)));
        sourceLink_.setFont(FontManager::getInstance().getUIFont(11.0f), false);
        addAndMakeVisible(sourceLink_);

        actionButton_.onClick = [this]() { handleActionClick(); };
        addAndMakeVisible(actionButton_);

        progressBar_.setVisible(false);
        addAndMakeVisible(progressBar_);

        refreshStatus();
    }

    void resized() final {
        auto bounds = getLocalBounds().reduced(12);
        statusLabel_.setBounds(bounds.removeFromTop(96));
        bounds.removeFromTop(4);
        sourceLink_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(8);
        auto row = bounds.removeFromTop(28);
        actionButton_.setBounds(row.removeFromLeft(200).reduced(0, 1));
        if (progressBar_.isVisible()) {
            bounds.removeFromTop(8);
            progressBar_.setBounds(bounds.removeFromTop(20));
        }
    }

    void apply(magda::Config&) const {}

  private:
    void refreshStatus() {
        const bool installed = magda::media::MediaModelDownloader::isInstalled(Bundle::BeatTracker);
        if (installed) {
            statusLabel_.setText(
                "Beat tracker is installed.\n\nA sample whose filename and header say nothing "
                "about its tempo has one measured from the audio when the library indexes it, "
                "and a clip can be put into beat mode without typing a BPM. Remove it to free "
                "the disk space; tempo then falls back to a weaker estimate.",
                juce::dontSendNotification);
            actionButton_.setButtonText("Remove");
        } else {
            const auto totalMb =
                magda::media::MediaModelDownloader::expectedTotalBytes(Bundle::BeatTracker) /
                (1024.0 * 1024.0);
            statusLabel_.setText(
                "Beat tracker is not installed.\n\nDownload (~" + juce::String(totalMb, 0) +
                    " MB) to measure the tempo of samples whose filename and header do not "
                    "carry one. Without it MAGDA falls back to an estimate that is right far "
                    "less often, and says nothing when it cannot tell.",
                juce::dontSendNotification);
            actionButton_.setButtonText("Download Beat Tracker");
        }
        progressBar_.setVisible(false);
        actionButton_.setEnabled(true);
        resized();
    }

    void handleActionClick() {
        if (magda::media::MediaModelDownloader::isInstalled(Bundle::BeatTracker)) {
            magda::media::MediaModelDownloader::remove(Bundle::BeatTracker);
            refreshStatus();
            return;
        }

        actionButton_.setEnabled(false);
        progressValue_ = 0.0;
        progressBar_.setVisible(true);
        resized();

        const juce::Component::SafePointer<BeatTrackerPage> self(this);
        downloader_.start([self](const magda::media::MediaModelDownloader::Progress& p) {
            if (self != nullptr) {
                self->onProgress(p);
            }
        });
    }

    void onProgress(const magda::media::MediaModelDownloader::Progress& p) {
        using Phase = magda::media::MediaModelDownloader::Phase;
        switch (p.phase) {
            case Phase::Downloading:
            case Phase::Verifying:
                if (p.totalBytesAll > 0) {
                    progressValue_ =
                        static_cast<double>(p.bytesDoneAll) / static_cast<double>(p.totalBytesAll);
                }
                statusLabel_.setText(
                    juce::String(p.phase == Phase::Verifying ? "Verifying " : "Downloading ") +
                        p.currentFilename,
                    juce::dontSendNotification);
                break;
            case Phase::Done:
                refreshStatus();
                break;
            case Phase::Failed:
                statusLabel_.setText("Download failed: " + p.errorMessage,
                                     juce::dontSendNotification);
                actionButton_.setEnabled(true);
                progressBar_.setVisible(false);
                break;
            case Phase::Cancelled:
            case Phase::Idle:
                refreshStatus();
                break;
        }
    }

    juce::Label statusLabel_;
    juce::HyperlinkButton sourceLink_;
    // ProgressBar holds a reference to the value, so the value is declared
    // first to be constructed first.
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_{progressValue_};
    juce::TextButton actionButton_;
    magda::media::MediaModelDownloader downloader_{Bundle::BeatTracker};
};

// ============================================================================
// StemSeparationPage — stem separation model weights (issue #1288)
// ============================================================================

class AISettingsPages::StemSeparationPage : public juce::Component {
  public:
    StemSeparationPage() {
        statusLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        statusLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
        statusLabel_.setJustificationType(juce::Justification::topLeft);
        statusLabel_.setText(
            "Split into Stems (right-click an audio clip) separates audio onto new tracks. "
            "HPSS needs no model; the engines below download their weights on demand.",
            juce::dontSendNotification);
        addAndMakeVisible(statusLabel_);

        locationCaption_.setText("Models location", juce::dontSendNotification);
        styleLabel(locationCaption_);
        addAndMakeVisible(locationCaption_);

        locationField_.setReadOnly(true);
        styleEditor(locationField_, "");
        locationField_.setText(magda::stems::StemModelDownloader::modelsDir().getFullPathName(),
                               juce::dontSendNotification);
        addAndMakeVisible(locationField_);

        rows_[0] = std::make_unique<ModelRow>(
            magda::stems::StemModel::Htdemucs,
            "Best quality, slowest. Weights are published by Meta for research use.");
        rows_[1] = std::make_unique<ModelRow>(magda::stems::StemModel::Spleeter2s,
                                              "Faster and lighter, vocals/accompaniment only.");
        for (auto& row : rows_)
            addAndMakeVisible(*row);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int rowH = 24;
        const int labelW = 110;

        statusLabel_.setBounds(bounds.removeFromTop(48));
        bounds.removeFromTop(8);

        auto locRow = bounds.removeFromTop(rowH);
        locationCaption_.setBounds(locRow.removeFromLeft(labelW));
        locationField_.setBounds(locRow.reduced(0, 1));
        bounds.removeFromTop(12);

        for (auto& row : rows_) {
            row->setBounds(bounds.removeFromTop(106));
            bounds.removeFromTop(10);
        }
    }

    void load(const magda::Config&) {
        for (auto& row : rows_)
            row->refreshStatus();
    }
    // Download / remove act immediately; nothing batches up for OK.
    void apply(magda::Config&) const {}

  private:
    // One downloadable model: name + status, progress bar, download/remove.
    class ModelRow : public juce::Component {
      public:
        ModelRow(magda::stems::StemModel model, juce::String blurb)
            : model_(model), blurb_(std::move(blurb)), downloader_(model) {
            nameLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
            nameLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
            addAndMakeVisible(nameLabel_);

            // Where the weights come from, as a clickable HuggingFace link.
            const juce::String url = magda::stems::StemModelDownloader::sourceUrl(model_);
            sourceLink_ = std::make_unique<juce::HyperlinkButton>(url.replace("https://", ""),
                                                                  juce::URL(url));
            sourceLink_->setFont(FontManager::getInstance().getUIFont(11.0f), false,
                                 juce::Justification::centredLeft);
            sourceLink_->setColour(juce::HyperlinkButton::textColourId,
                                   ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
            addAndMakeVisible(*sourceLink_);

            progressBar_.setColour(juce::ProgressBar::backgroundColourId,
                                   ActiveTheme::getColour(ActiveTheme::BACKGROUND).brighter(0.05f));
            progressBar_.setColour(juce::ProgressBar::foregroundColourId,
                                   ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
            progressBar_.setPercentageDisplay(false);
            progressBar_.setVisible(false);
            addAndMakeVisible(progressBar_);

            actionButton_.onClick = [this]() { handleActionClick(); };
            addAndMakeVisible(actionButton_);

            refreshStatus();
        }

        void resized() override {
            auto bounds = getLocalBounds();
            nameLabel_.setBounds(bounds.removeFromTop(30));
            sourceLink_->setBounds(bounds.removeFromTop(16));
            bounds.removeFromTop(4);
            progressBar_.setBounds(bounds.removeFromTop(18));
            bounds.removeFromTop(6);
            actionButton_.setBounds(bounds.removeFromTop(32).removeFromLeft(280));
        }

        void refreshStatus() {
            const auto sizeMb = juce::String(
                magda::stems::StemModelDownloader::expectedTotalBytes(model_) / (1024 * 1024));
            const bool installed = magda::stems::StemModelDownloader::isInstalled(model_);
            const juce::String name = magda::stems::StemModelDownloader::displayName(model_);

            nameLabel_.setText(name + " - " + blurb_ + (installed ? " Installed." : ""),
                               juce::dontSendNotification);
            actionButton_.setButtonText(installed ? "Remove " + name
                                                  : "Download " + name + " (" + sizeMb + " MB)");
        }

      private:
        void handleActionClick() {
            if (downloader_.isRunning()) {
                downloader_.cancel();
                return;
            }
            if (magda::stems::StemModelDownloader::isInstalled(model_)) {
                magda::stems::StemModelDownloader::remove(model_);
                refreshStatus();
                return;
            }

            progressValue_ = 0.0;
            progressBar_.setVisible(true);
            actionButton_.setButtonText("Cancel");

            // SafePointer: the download outlives dialog dismissal, and the
            // callback hops through callAsync.
            juce::Component::SafePointer<ModelRow> safe(this);
            downloader_.start([safe](const magda::stems::StemModelDownloader::Progress& p) {
                auto* self = safe.getComponent();
                if (self == nullptr)
                    return;

                using Phase = magda::stems::StemModelDownloader::Phase;
                if (p.phase == Phase::Downloading && p.bytesTotal > 0) {
                    self->progressValue_ =
                        static_cast<double>(p.bytesDone) / static_cast<double>(p.bytesTotal);
                } else if (p.phase == Phase::Verifying) {
                    self->progressValue_ = 1.0;
                } else if (p.phase == Phase::Done || p.phase == Phase::Failed ||
                           p.phase == Phase::Cancelled) {
                    self->progressBar_.setVisible(false);
                    self->refreshStatus();
                    if (p.phase == Phase::Failed) {
                        self->nameLabel_.setText("Download failed: " + p.errorMessage,
                                                 juce::dontSendNotification);
                    }
                }
            });
        }

        magda::stems::StemModel model_;
        juce::String blurb_;
        juce::Label nameLabel_;
        std::unique_ptr<juce::HyperlinkButton> sourceLink_;
        // ProgressBar holds a reference to the value; declare the value first.
        double progressValue_ = 0.0;
        juce::ProgressBar progressBar_{progressValue_};
        juce::TextButton actionButton_;
        magda::stems::StemModelDownloader downloader_;
    };

    juce::Label statusLabel_;
    juce::Label locationCaption_;
    juce::TextEditor locationField_;
    std::array<std::unique_ptr<ModelRow>, 2> rows_;
};

// ============================================================================
// CommandModelPage — the encoder command model (#1847)
//
// Not bundled with the app: ~450 MB, and the built-in 51k conv net is always
// available as the fallback. Downloading this replaces it for the "Fast
// Inference (Command)" provider, taking held-out accuracy from 46.5% to 91.5%.
// ============================================================================

class AISettingsPages::CommandModelPage : public juce::Component {
  public:
    CommandModelPage() {
        blurbLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        blurbLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
        blurbLabel_.setJustificationType(juce::Justification::topLeft);
        blurbLabel_.setText(
            "Fast Inference runs Command requests locally and works immediately with MAGDA's "
            "built-in basic model. Download the larger model for better understanding of natural "
            "language. Cloud providers do not use this model.",
            juce::dontSendNotification);
        addAndMakeVisible(blurbLabel_);

        locationCaption_.setText("Models location", juce::dontSendNotification);
        styleLabel(locationCaption_);
        addAndMakeVisible(locationCaption_);

        locationField_.setReadOnly(true);
        styleEditor(locationField_, "");
        addAndMakeVisible(locationField_);

        browseButton_.setButtonText("Browse...");
        browseButton_.onClick = [this]() { browseForLocation(); };
        addAndMakeVisible(browseButton_);

        resetLocationButton_.setButtonText("Reset");
        resetLocationButton_.onClick = [this]() {
            magda::Config::getInstance().setCommandModelModelsDir(std::string{});
            magda::Config::getInstance().save();
            refreshStatus();
        };
        addAndMakeVisible(resetLocationButton_);

        nameLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        nameLabel_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
        addAndMakeVisible(nameLabel_);

        const juce::String url = magda::CommandModelDownloader::sourceUrl();
        sourceLink_ =
            std::make_unique<juce::HyperlinkButton>(url.replace("https://", ""), juce::URL(url));
        sourceLink_->setFont(FontManager::getInstance().getUIFont(11.0f), false,
                             juce::Justification::centredLeft);
        sourceLink_->setColour(juce::HyperlinkButton::textColourId,
                               ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        addAndMakeVisible(*sourceLink_);

        progressBar_.setColour(juce::ProgressBar::backgroundColourId,
                               ActiveTheme::getColour(ActiveTheme::BACKGROUND).brighter(0.05f));
        progressBar_.setColour(juce::ProgressBar::foregroundColourId,
                               ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
        progressBar_.setPercentageDisplay(false);
        progressBar_.setVisible(false);
        addAndMakeVisible(progressBar_);

        actionButton_.onClick = [this]() { handleActionClick(); };
        addAndMakeVisible(actionButton_);

        refreshStatus();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        const int rowH = 24;
        const int labelW = 110;

        blurbLabel_.setBounds(bounds.removeFromTop(56));
        bounds.removeFromTop(8);

        auto locRow = bounds.removeFromTop(rowH);
        locationCaption_.setBounds(locRow.removeFromLeft(labelW));
        resetLocationButton_.setBounds(locRow.removeFromRight(70).reduced(0, 1));
        locRow.removeFromRight(4);
        browseButton_.setBounds(locRow.removeFromRight(90).reduced(0, 1));
        locRow.removeFromRight(4);
        locationField_.setBounds(locRow.reduced(0, 1));
        bounds.removeFromTop(12);

        nameLabel_.setBounds(bounds.removeFromTop(30));
        sourceLink_->setBounds(bounds.removeFromTop(16));
        bounds.removeFromTop(4);
        progressBar_.setBounds(bounds.removeFromTop(18));
        bounds.removeFromTop(6);
        actionButton_.setBounds(bounds.removeFromTop(26).removeFromLeft(240));
    }

    void load(const magda::Config&) {
        refreshStatus();
    }
    // Download / remove act immediately; nothing batches up for OK.
    void apply(magda::Config&) const {}

  private:
    void refreshStatus() {
        locationField_.setText(magda::CommandModelDownloader::modelsDir().getFullPathName(),
                               juce::dontSendNotification);
        const auto sizeMb =
            juce::String(magda::CommandModelDownloader::expectedTotalBytes() / (1024 * 1024));
        const bool installed = magda::CommandModelDownloader::isInstalled();
        const bool downloading = downloader_.isRunning();
        const juce::String name = magda::CommandModelDownloader::displayName();

        nameLabel_.setText(name + (installed ? " - Installed. Used automatically by Fast "
                                               "Inference (Command)."
                                             : " - Not installed. The built-in model is in use."),
                           juce::dontSendNotification);
        actionButton_.setButtonText(installed ? "Remove" : "Download (" + sizeMb + " MB)");
        browseButton_.setEnabled(!downloading);
        resetLocationButton_.setEnabled(!downloading);
    }

    void browseForLocation() {
        const auto currentDir = magda::CommandModelDownloader::modelsDir();
        fileChooser_ = std::make_unique<juce::FileChooser>(
            "Choose a folder for the Command model",
            currentDir.exists() ? currentDir
                                : juce::File::getSpecialLocation(juce::File::userHomeDirectory));
        const juce::Component::SafePointer<CommandModelPage> self(this);
        fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                      juce::FileBrowserComponent::canSelectDirectories,
                                  [self](const juce::FileChooser& fc) {
                                      if (self == nullptr)
                                          return;
                                      const auto picked = fc.getResult();
                                      if (!picked.isDirectory())
                                          return;
                                      magda::Config::getInstance().setCommandModelModelsDir(
                                          picked.getFullPathName().toStdString());
                                      magda::Config::getInstance().save();
                                      self->refreshStatus();
                                  });
    }

    void handleActionClick() {
        if (downloader_.isRunning()) {
            downloader_.cancel();
            return;
        }
        if (magda::CommandModelDownloader::isInstalled()) {
            magda::CommandModelDownloader::remove();
            refreshStatus();
            return;
        }

        progressValue_ = 0.0;
        progressBar_.setVisible(true);
        actionButton_.setButtonText("Cancel");
        browseButton_.setEnabled(false);
        resetLocationButton_.setEnabled(false);

        // SafePointer: the download outlives dialog dismissal and the callback
        // hops through callAsync.
        juce::Component::SafePointer<CommandModelPage> safe(this);
        downloader_.start([safe](const magda::CommandModelDownloader::Progress& p) {
            auto* self = safe.getComponent();
            if (self == nullptr)
                return;

            using Phase = magda::CommandModelDownloader::Phase;
            if (p.phase == Phase::Downloading && p.bytesTotal > 0) {
                self->progressValue_ =
                    static_cast<double>(p.bytesDone) / static_cast<double>(p.bytesTotal);
            } else if (p.phase == Phase::Verifying) {
                self->progressValue_ = 1.0;
            } else if (p.phase == Phase::Done || p.phase == Phase::Failed ||
                       p.phase == Phase::Cancelled) {
                self->progressBar_.setVisible(false);
                self->refreshStatus();
                self->browseButton_.setEnabled(true);
                self->resetLocationButton_.setEnabled(true);
                if (p.phase == Phase::Failed) {
                    self->nameLabel_.setText("Download failed: " + p.errorMessage,
                                             juce::dontSendNotification);
                }
            }
        });
    }

    juce::Label blurbLabel_;
    juce::Label locationCaption_;
    juce::TextEditor locationField_;
    juce::TextButton browseButton_;
    juce::TextButton resetLocationButton_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::Label nameLabel_;
    std::unique_ptr<juce::HyperlinkButton> sourceLink_;
    // ProgressBar holds a reference to the value; declare the value first.
    double progressValue_ = 0.0;
    juce::ProgressBar progressBar_{progressValue_};
    juce::TextButton actionButton_;
    magda::CommandModelDownloader downloader_;
};

// ============================================================================
// ModelDownloadsPage — one place for every optional downloadable model.
//
// The category selector keeps the individual model workflows compact while
// avoiding separate top-level tabs as new model families are added.
// ============================================================================

class AISettingsPages::ModelDownloadsPage : public juce::Component {
  public:
    ModelDownloadsPage(LocalPage& localPage, SampleTaggerPage* samplePage,
                       BeatTrackerPage* beatPage, StemSeparationPage* stemsPage,
                       CommandModelPage* commandPage)
        : localPage_(localPage),
          samplePage_(samplePage),
          beatPage_(beatPage),
          stemsPage_(stemsPage),
          commandPage_(commandPage) {
        modelHeader_.setText("Model", juce::dontSendNotification);
        daw::ui::SettingsLookAndFeel::markSectionHeader(modelHeader_);
        addAndMakeVisible(modelHeader_);

        categoryLabel_.setText("Type", juce::dontSendNotification);
        styleLabel(categoryLabel_, 13.0f);
        addAndMakeVisible(categoryLabel_);

        categoryCombo_.addItem("Local LLM", kLocal);
        if (samplePage_ != nullptr)
            categoryCombo_.addItem("Sample analysis", kSampleAnalyzer);
        if (beatPage_ != nullptr)
            categoryCombo_.addItem("Tempo detection", kBeatTracker);
        if (stemsPage_ != nullptr)
            categoryCombo_.addItem("Stem separation", kStems);
        if (commandPage_ != nullptr)
            categoryCombo_.addItem("Command model", kCommandModel);
        categoryCombo_.setSelectedId(kLocal, juce::dontSendNotification);
        categoryCombo_.onChange = [this]() { updateVisiblePage(); };
        styleCombo(categoryCombo_);
        addAndMakeVisible(categoryCombo_);

        addAndMakeVisible(localPage_);
        if (samplePage_ != nullptr)
            addAndMakeVisible(*samplePage_);
        if (beatPage_ != nullptr)
            addAndMakeVisible(*beatPage_);
        if (stemsPage_ != nullptr)
            addAndMakeVisible(*stemsPage_);
        if (commandPage_ != nullptr)
            addAndMakeVisible(*commandPage_);
        updateVisiblePage();
    }

    juce::Component& getToolbar() {
        return localPage_.getBadge();
    }

    void resized() final {
        auto bounds = getLocalBounds().reduced(0, 8);
        modelHeader_.setBounds(bounds.removeFromTop(30));
        categoryRow_ = bounds.removeFromTop(36);
        auto categoryRow = categoryRow_;
        categoryCombo_.setBounds(categoryRow.removeFromRight(190).reduced(0, 5));
        categoryLabel_.setBounds(categoryRow);

        localPage_.setBounds(bounds);
        if (samplePage_ != nullptr)
            samplePage_->setBounds(bounds);
        if (beatPage_ != nullptr)
            beatPage_->setBounds(bounds);
        if (stemsPage_ != nullptr)
            stemsPage_->setBounds(bounds);
        if (commandPage_ != nullptr)
            commandPage_->setBounds(bounds);
    }

    // Preserve existing callers that deep-link to the former download tabs.
    bool selectLegacyCategory(const juce::String& tabName) {
        if (tabName == "Local") {
            categoryCombo_.setSelectedId(kLocal, juce::sendNotification);
            return true;
        }
        if (tabName == "Sample Analyzer" && samplePage_ != nullptr) {
            categoryCombo_.setSelectedId(kSampleAnalyzer, juce::sendNotification);
            return true;
        }
        if (tabName == "Stems" && stemsPage_ != nullptr) {
            categoryCombo_.setSelectedId(kStems, juce::sendNotification);
            return true;
        }
        return false;
    }

  private:
    enum Category { kLocal = 1, kSampleAnalyzer, kBeatTracker, kStems, kCommandModel };

    void updateVisiblePage() {
        const int category = categoryCombo_.getSelectedId();
        localPage_.setVisible(category == kLocal);
        if (samplePage_ != nullptr)
            samplePage_->setVisible(category == kSampleAnalyzer);
        if (beatPage_ != nullptr)
            beatPage_->setVisible(category == kBeatTracker);
        if (stemsPage_ != nullptr)
            stemsPage_->setVisible(category == kStems);
        if (commandPage_ != nullptr)
            commandPage_->setVisible(category == kCommandModel);
        resized();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour().withMultipliedAlpha(0.6f));
        g.fillRect(0, categoryRow_.getBottom() - 1, getWidth(), 1);
    }

    LocalPage& localPage_;
    juce::Label modelHeader_;
    juce::Rectangle<int> categoryRow_;
    SampleTaggerPage* samplePage_;
    BeatTrackerPage* beatPage_;
    StemSeparationPage* stemsPage_;
    CommandModelPage* commandPage_;
    juce::Label categoryLabel_;
    juce::ComboBox categoryCombo_;
};

// ============================================================================
// AISettingsPages
// ============================================================================

AISettingsPages::AISettingsPages() {
    cloudPage_ = std::make_unique<CloudPage>();
    localPage_ = std::make_unique<LocalPage>();
    configPage_ = std::make_unique<ConfigPage>();
    if constexpr (magda::media::clapBackendAvailable()) {
        samplePage_ = std::make_unique<SampleTaggerPage>();
    }
    if constexpr (magda::stems::DemucsSeparator::backendAvailable()) {
        beatTrackerPage_ = std::make_unique<BeatTrackerPage>();
        stemsPage_ = std::make_unique<StemSeparationPage>();
    }
    // The encoder command model runs on ONNX Runtime, same availability gate
    // as the CLAP-backed pages: no runtime, no download worth offering.
    if constexpr (magda::media::clapBackendAvailable()) {
        commandModelPage_ = std::make_unique<CommandModelPage>();
    }
    modelDownloadsPage_ =
        std::make_unique<ModelDownloadsPage>(*localPage_, samplePage_.get(), beatTrackerPage_.get(),
                                             stemsPage_.get(), commandModelPage_.get());

    configPage_->cloudPage = cloudPage_.get();
    configPage_->localPage = localPage_.get();
}

AISettingsPages::~AISettingsPages() = default;

juce::Component& AISettingsPages::cloudPage() {
    return *cloudPage_;
}

juce::Component& AISettingsPages::configPage() {
    return *configPage_;
}

juce::Component& AISettingsPages::modelsPage() {
    return *modelDownloadsPage_;
}

juce::Component& AISettingsPages::cloudToolbar() {
    return cloudPage_->getToolbar();
}

juce::Component& AISettingsPages::configToolbar() {
    return configPage_->getToolbar();
}

juce::Component& AISettingsPages::modelsToolbar() {
    return modelDownloadsPage_->getToolbar();
}

void AISettingsPages::configShown() {
    configPage_->refreshOnShow();
}

bool AISettingsPages::selectLegacyCategory(const juce::String& name) {
    return modelDownloadsPage_->selectLegacyCategory(name);
}

void AISettingsPages::load(Config& config) {
    cloudPage_->load(config);
    localPage_->load(config);
    configPage_->load(config);
    if (samplePage_)
        samplePage_->load(config);
    if (stemsPage_)
        stemsPage_->load(config);
    if (commandModelPage_)
        commandModelPage_->load(config);
}

void AISettingsPages::apply(Config& config) {
    cloudPage_->apply(config);
    localPage_->apply(config);
    configPage_->apply(config);
    if (samplePage_)
        samplePage_->apply(config);
    if (stemsPage_)
        stemsPage_->apply(config);
    if (commandModelPage_)
        commandModelPage_->apply(config);
}

namespace {

/** @brief Cloud providers, agent mapping and local models; Apply commits them. */
class AISection : public SettingsSection {
  public:
    AISection() : SettingsSection(SettingsSectionId::AI) {}

    juce::String getTitle() const override {
        return tr("settings.section.ai");
    }
    // The page changed most comes first.
    std::vector<Tab> getTabs() override {
        return {{"Assistant", &pages_.configPage(), {}, &pages_.configToolbar()},
                {"Providers", &pages_.cloudPage(), {}, &pages_.cloudToolbar()},
                {"Local models", &pages_.modelsPage(), {}, &pages_.modelsToolbar()}};
    }
    void load(Config& config) override {
        pages_.load(config);
    }
    void writeTo(Config& config) override {
        pages_.apply(config);
    }
    void tabShown(int tab) override {
        if (tab == kConfigTab)
            pages_.configShown();
    }
    // The old tab names, and the Models categories that used to be tabs.
    int findTab(const juce::String& name) override {
        if (name == "Config")
            return kConfigTab;
        if (name == "Cloud")
            return kProvidersTab;
        if (name == "Models")
            return kModelsTab;
        return pages_.selectLegacyCategory(name) ? kModelsTab : -1;
    }

  private:
    static constexpr int kConfigTab = 0;
    static constexpr int kProvidersTab = 1;
    static constexpr int kModelsTab = 2;
    AISettingsPages pages_;
};

}  // namespace

std::unique_ptr<SettingsSection> createAISection() {
    return std::make_unique<AISection>();
}

}  // namespace magda
