#include "SettingsWindow.hpp"

#include <algorithm>

#include "SettingsSections.hpp"
#include "core/Config.hpp"
#include "core/StringTable.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SettingsLookAndFeel.hpp"

namespace magda {

namespace {

juce::ApplicationCommandManager* contextCommandManager = nullptr;
AudioEngine* contextAudioEngine = nullptr;
juce::Component::SafePointer<SettingsWindow> openWindow;
// Closed but not yet deleted: deletion is async, out of the button callback.
juce::Component::SafePointer<SettingsWindow> closingWindow;

void collectText(juce::Component& component, juce::StringArray& out) {
    if (auto* label = dynamic_cast<juce::Label*>(&component))
        out.add(label->getText());
    else if (auto* button = dynamic_cast<juce::Button*>(&component))
        out.add(button->getButtonText());
    else if (auto* combo = dynamic_cast<juce::ComboBox*>(&component))
        out.add(combo->getText());
    for (auto* child : component.getChildren())
        collectText(*child, out);
}

/** @brief A sidebar entry: the section title and its pending-change count. */
class SidebarRow : public juce::Component {
  public:
    std::function<void()> onClick;

    void setTitle(const juce::String& title) {
        title_ = title;
        repaint();
    }
    void setSelected(bool selected) {
        if (selected_ != selected) {
            selected_ = selected;
            repaint();
        }
    }
    void setCount(int count) {
        if (count_ != count) {
            count_ = count;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(6.0f, 1.0f);
        if (selected_) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE));
            g.fillRoundedRectangle(bounds, 4.0f);
        } else if (isMouseOver()) {
            g.setColour(ActiveTheme::getColour(ActiveTheme::SURFACE).withAlpha(0.5f));
            g.fillRoundedRectangle(bounds, 4.0f);
        }
        auto text = bounds.reduced(10.0f, 0.0f);
        if (count_ > 0) {
            g.setColour(ActiveTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText(juce::String(count_), text.removeFromRight(24.0f),
                       juce::Justification::centredRight);
        }
        g.setColour(ActiveTheme::getColour(selected_ ? ActiveTheme::TEXT_PRIMARY
                                                     : ActiveTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawText(title_, text, juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override {
        repaint();
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (onClick && getLocalBounds().contains(e.getPosition()))
            onClick();
    }

  private:
    juce::String title_;
    bool selected_ = false;
    int count_ = 0;
};

/** @brief The tabs of the current section, underlined when selected. */
class TabStrip : public juce::Component {
  public:
    std::function<void(int)> onSelect;

    void setTabs(juce::StringArray titles, int selected) {
        titles_ = std::move(titles);
        selected_ = selected;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(ActiveTheme::getBorderColour());
        g.fillRect(getLocalBounds().removeFromBottom(1));

        const auto font = FontManager::getInstance().getUIFont(13.0f);
        g.setFont(font);
        rects_.clear();
        int x = 0;
        for (int i = 0; i < titles_.size(); ++i) {
            const int w = juce::GlyphArrangement::getStringWidthInt(font, titles_[i]) + 24;
            juce::Rectangle<int> r(x, 0, w, getHeight());
            rects_.push_back(r);
            const bool on = i == selected_;
            g.setColour(ActiveTheme::getColour(on ? ActiveTheme::TEXT_PRIMARY
                                                  : ActiveTheme::TEXT_SECONDARY));
            g.drawText(titles_[i], r, juce::Justification::centred);
            if (on) {
                g.setColour(ActiveTheme::getAccentColour());
                g.fillRect(r.removeFromBottom(2).reduced(8, 0));
            }
            x += w;
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        for (size_t i = 0; i < rects_.size(); ++i)
            if (rects_[i].contains(e.getPosition()) && onSelect)
                onSelect(static_cast<int>(i));
    }

  private:
    juce::StringArray titles_;
    int selected_ = 0;
    std::vector<juce::Rectangle<int>> rects_;
};

}  // namespace

// ============================================================================
// Panel
// ============================================================================

class SettingsWindow::Panel : public juce::Component, private juce::Timer {
  public:
    explicit Panel(std::function<void()> close) : close_(std::move(close)) {
        setLookAndFeel(&daw::ui::SettingsLookAndFeel::getInstance());
        setWantsKeyboardFocus(true);

        for (auto& section : createPreferencesSections(contextCommandManager))
            sections_.push_back(std::move(section));
        sections_.push_back(createAudioMidiSection(contextAudioEngine));
        sections_.push_back(createControllersSection());
        sections_.push_back(createConnectionsSection());
        sections_.push_back(createPluginsSection());
        sections_.push_back(createAISection());
        std::ranges::stable_sort(sections_, {}, [](const auto& s) { return s->getId(); });

        auto& config = Config::getInstance();
        for (auto& section : sections_) {
            tabs_.push_back(section->getTabs());
            section->load(config);
            section->captureBaseline();

            auto row = std::make_unique<SidebarRow>();
            row->setTitle(section->getTitle());
            row->onClick = [this, index = static_cast<int>(rows_.size())] { selectSection(index); };
            addAndMakeVisible(*row);
            rows_.push_back(std::move(row));
        }

        searchBox_.setTextToShowWhenEmpty(tr("settings.search"),
                                          ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        searchBox_.onTextChange = [this] { applyFilter(); };
        searchBox_.onEscapeKey = [this] {
            if (searchBox_.isEmpty())
                close_();
            else
                searchBox_.clear();
            applyFilter();
        };
        addAndMakeVisible(searchBox_);

        tabStrip_.onSelect = [this](int visibleIndex) {
            if (visibleIndex >= 0 && visibleIndex < static_cast<int>(visibleTabs_.size()))
                showTab(visibleTabs_[static_cast<size_t>(visibleIndex)]);
        };
        addAndMakeVisible(tabStrip_);

        viewport_.setScrollBarsShown(true, false, true, false);
        viewport_.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
        addAndMakeVisible(viewport_);

        liveNote_.setText(tr("settings.applies_immediately"), juce::dontSendNotification);
        liveNote_.setFont(FontManager::getInstance().getUIFont(12.0f));
        liveNote_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        addChildComponent(liveNote_);

        noMatches_.setText(tr("settings.no_matches"), juce::dontSendNotification);
        noMatches_.setJustificationType(juce::Justification::centred);
        noMatches_.setColour(juce::Label::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        addChildComponent(noMatches_);

        changesLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
        changesLabel_.setColour(juce::Label::textColourId,
                                ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        addAndMakeVisible(changesLabel_);

        restoreButton_.setButtonText(tr("settings.restore_defaults"));
        daw::ui::SettingsLookAndFeel::markLinkButton(restoreButton_);
        restoreButton_.onClick = [this] {
            if (auto* section = currentSection())
                section->restoreDefaults();
            updateCounts();
        };
        addAndMakeVisible(restoreButton_);

        cancelButton_.setButtonText(tr("dialogs.cancel"));
        cancelButton_.addShortcut(juce::KeyPress(juce::KeyPress::escapeKey));
        cancelButton_.onClick = [this] { close_(); };
        addAndMakeVisible(cancelButton_);

        applyButton_.setButtonText(tr("dialogs.apply"));
        applyButton_.onClick = [this] { applyAll(); };
        addAndMakeVisible(applyButton_);

        okButton_.setButtonText(tr("dialogs.ok"));
        okButton_.addShortcut(juce::KeyPress(juce::KeyPress::returnKey));
        MagdaAlertWindow::setRole(okButton_, MagdaAlertWindow::ButtonRole::Primary);
        okButton_.onClick = [this] {
            applyAll();
            close_();
        };
        addAndMakeVisible(okButton_);

        selectSection(0);
        updateCounts();
        startTimerHz(4);
    }

    ~Panel() override {
        stopTimer();
        viewport_.setViewedComponent(nullptr, false);
        sections_.clear();
        setLookAndFeel(nullptr);
    }

    void open(SettingsSectionId id, const juce::String& tabName) {
        if (!searchBox_.isEmpty()) {
            searchBox_.clear();
            applyFilter();
        }
        for (size_t i = 0; i < sections_.size(); ++i) {
            if (sections_[i]->getId() != id)
                continue;
            selectSection(static_cast<int>(i));
            if (tabName.isEmpty())
                return;
            const auto& tabs = tabs_[i];
            for (size_t t = 0; t < tabs.size(); ++t)
                if (tabs[t].title == tabName)
                    return showTab(static_cast<int>(t));
            if (const int t = sections_[i]->findTab(tabName); t >= 0)
                showTab(t);
            return;
        }
    }

    void windowClosing() {
        for (auto& section : sections_)
            section->windowClosing();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND));
        g.setColour(ActiveTheme::getBackgroundColour());
        g.fillRect(sidebarBounds_);
        g.setColour(ActiveTheme::getBorderColour());
        g.fillRect(sidebarBounds_.getRight(), 0, 1, sidebarBounds_.getHeight());
        g.fillRect(footerBounds_.withHeight(1));
        if (pendingTotal_ > 0) {
            g.setColour(ActiveTheme::getAccentColour());
            const auto dot = changesLabel_.getBounds().withWidth(10).toFloat();
            g.fillEllipse(dot.withSizeKeepingCentre(5.0f, 5.0f).translated(-6.0f, 0.0f));
        }
    }

    void resized() override {
        auto bounds = getLocalBounds();
        footerBounds_ = bounds.removeFromBottom(58);
        sidebarBounds_ = bounds.removeFromLeft(kSidebarWidth);

        auto side = sidebarBounds_.reduced(10, 12);
        searchBox_.setBounds(side.removeFromTop(28));
        side.removeFromTop(10);
        for (auto& row : rows_) {
            if (!row->isVisible())
                continue;
            row->setBounds(side.removeFromTop(30).expanded(10, 0));
        }

        auto content = bounds.reduced(20, 0);
        const bool hasTabs = tabStrip_.isVisible();
        if (hasTabs)
            tabStrip_.setBounds(content.removeFromTop(40).withTrimmedTop(8));
        else
            content.removeFromTop(12);
        if (liveNote_.isVisible())
            liveNote_.setBounds(content.removeFromTop(28));
        viewport_.setBounds(content.withTrimmedRight(-20));
        noMatches_.setBounds(content);
        layoutPage();

        auto footer = footerBounds_.reduced(20, 12);
        okButton_.setBounds(footer.removeFromRight(72));
        footer.removeFromRight(8);
        applyButton_.setBounds(footer.removeFromRight(80));
        footer.removeFromRight(8);
        cancelButton_.setBounds(footer.removeFromRight(108));
        if (pendingTotal_ > 0) {
            footer.removeFromLeft(10);  // the dot
            const int width = juce::GlyphArrangement::getStringWidthInt(changesLabel_.getFont(),
                                                                        changesLabel_.getText());
            changesLabel_.setBounds(footer.removeFromLeft(width + 8));
            footer.removeFromLeft(16);
        } else {
            changesLabel_.setBounds({});
        }
        restoreButton_.setBounds(footer.removeFromLeft(130));
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            close_();
            return true;
        }
        if (key == juce::KeyPress('f', juce::ModifierKeys::commandModifier, 0)) {
            searchBox_.grabKeyboardFocus();
            return true;
        }
        return false;
    }

    void lookAndFeelChanged() override {
        refreshHostWindowBackground(*this);
        repaint();
    }

  private:
    static constexpr int kSidebarWidth = 190;

    std::function<void()> close_;
    std::vector<std::unique_ptr<SettingsSection>> sections_;
    std::vector<std::vector<SettingsSection::Tab>> tabs_;
    std::vector<std::unique_ptr<SidebarRow>> rows_;
    std::vector<int> visibleTabs_;
    int currentSection_ = -1;
    int currentTab_ = -1;
    int pendingTotal_ = 0;

    juce::TextEditor searchBox_;
    TabStrip tabStrip_;
    juce::Viewport viewport_;
    juce::Label liveNote_;
    juce::Label noMatches_;
    juce::Label changesLabel_;
    juce::TextButton restoreButton_;
    juce::TextButton cancelButton_;
    juce::TextButton applyButton_;
    juce::TextButton okButton_;
    juce::Rectangle<int> sidebarBounds_;
    juce::Rectangle<int> footerBounds_;

    SettingsSection* currentSection() const {
        if (currentSection_ < 0)
            return nullptr;
        return sections_[static_cast<size_t>(currentSection_)].get();
    }

    bool tabMatches(size_t section, size_t tab, const juce::String& query) const {
        if (query.isEmpty())
            return true;
        if (sections_[section]->getTitle().containsIgnoreCase(query))
            return true;
        const auto& t = tabs_[section][tab];
        if (t.title.containsIgnoreCase(query))
            return true;
        if (t.component == nullptr)
            return false;
        juce::StringArray text;
        collectText(*t.component, text);
        for (const auto& s : text)
            if (s.containsIgnoreCase(query))
                return true;
        return false;
    }

    std::vector<int> matchingTabs(size_t section) const {
        const auto query = searchBox_.getText().trim();
        std::vector<int> out;
        for (size_t t = 0; t < tabs_[section].size(); ++t)
            if (tabMatches(section, t, query))
                out.push_back(static_cast<int>(t));
        return out;
    }

    void applyFilter() {
        int firstVisible = -1;
        for (size_t i = 0; i < sections_.size(); ++i) {
            const bool visible = !matchingTabs(i).empty();
            rows_[i]->setVisible(visible);
            if (visible && firstVisible < 0)
                firstVisible = static_cast<int>(i);
        }
        const bool any = firstVisible >= 0;
        noMatches_.setVisible(!any);
        viewport_.setVisible(any);
        if (!any) {
            tabStrip_.setVisible(false);
            liveNote_.setVisible(false);
        } else if (currentSection_ < 0 ||
                   !rows_[static_cast<size_t>(currentSection_)]->isVisible()) {
            selectSection(firstVisible);
        } else {
            selectSection(currentSection_);
        }
        resized();
    }

    void selectSection(int index) {
        currentSection_ = index;
        for (size_t i = 0; i < rows_.size(); ++i)
            rows_[i]->setSelected(static_cast<int>(i) == index);

        auto& section = *sections_[static_cast<size_t>(index)];
        visibleTabs_ = matchingTabs(static_cast<size_t>(index));
        liveNote_.setVisible(section.appliesImmediately());
        restoreButton_.setEnabled(!section.appliesImmediately());

        const bool keepTab = std::ranges::find(visibleTabs_, currentTab_) != visibleTabs_.end();
        showTab(keepTab ? currentTab_ : (visibleTabs_.empty() ? 0 : visibleTabs_.front()));
    }

    void showTab(int tab) {
        currentTab_ = tab;
        const auto& tabs = tabs_[static_cast<size_t>(currentSection_)];
        juce::StringArray titles;
        int selected = 0;
        for (size_t i = 0; i < visibleTabs_.size(); ++i) {
            titles.add(tabs[static_cast<size_t>(visibleTabs_[i])].title);
            if (visibleTabs_[i] == tab)
                selected = static_cast<int>(i);
        }
        tabStrip_.setTabs(titles, selected);
        tabStrip_.setVisible(titles.size() > 1);

        if (tab >= 0 && tab < static_cast<int>(tabs.size())) {
            viewport_.setViewedComponent(tabs[static_cast<size_t>(tab)].component, false);
            sections_[static_cast<size_t>(currentSection_)]->tabShown(tab);
        }
        viewport_.setViewPosition(0, 0);
        resized();
    }

    void layoutPage() {
        if (currentSection_ < 0 || currentTab_ < 0)
            return;
        const auto& tab =
            tabs_[static_cast<size_t>(currentSection_)][static_cast<size_t>(currentTab_)];
        if (tab.component == nullptr)
            return;
        // Twice: the first pass can add a scrollbar that narrows the page.
        for (int pass = 0; pass < 2; ++pass) {
            const int width = juce::jmax(1, viewport_.getMaximumVisibleWidth() - 20);
            const int height = juce::jmax(1, viewport_.getMaximumVisibleHeight());
            const int wanted = tab.preferredHeight ? tab.preferredHeight(width) : height;
            tab.component->setSize(width, juce::jmax(height, wanted));
        }
    }

    void timerCallback() override {
        updateCounts();
    }

    void updateCounts() {
        int total = 0;
        for (size_t i = 0; i < sections_.size(); ++i) {
            const int count = sections_[i]->appliesImmediately() ? 0 : sections_[i]->countChanges();
            rows_[i]->setCount(count);
            total += count;
        }
        if (total != pendingTotal_) {
            pendingTotal_ = total;
            changesLabel_.setText(total == 0 ? juce::String()
                                  : total == 1
                                      ? tr("settings.one_change")
                                      : tr("settings.changes").replace("{0}", juce::String(total)),
                                  juce::dontSendNotification);
            resized();
            repaint();
        }
        applyButton_.setEnabled(total > 0);
    }

    void applyAll() {
        auto& config = Config::getInstance();
        const auto before = config.clone();

        // Paths commits last: a new data folder ends the session.
        std::vector<SettingsSection*> buffered;
        for (auto& section : sections_)
            if (!section->appliesImmediately())
                buffered.push_back(section.get());
        std::ranges::stable_partition(
            buffered, [](auto* s) { return s->getId() != SettingsSectionId::Paths; });

        for (auto* section : buffered)
            section->writeTo(config);
        config.save();
        for (auto* section : buffered)
            section->commit(*before);
        for (auto* section : buffered)
            section->captureBaseline();
        updateCounts();
    }
};

// ============================================================================
// SettingsWindow
// ============================================================================

SettingsWindow::SettingsWindow()
    : juce::DocumentWindow(tr("settings.title"),
                           ActiveTheme::getColour(ActiveTheme::PANEL_BACKGROUND),
                           juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    auto* panel = new Panel([this] { closeButtonPressed(); });
    panel_ = panel;
    setContentOwned(panel, false);
    setResizable(true, false);
    setResizeLimits(760, 480, 4000, 3000);

    auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                    ? juce::Desktop::getInstance()
                          .getDisplays()
                          .getPrimaryDisplay()
                          ->userBounds.toNearestInt()
                    : juce::Rectangle<int>(0, 0, 1280, 800);
    centreWithSize(juce::jmin(980, area.getWidth() - 48), juce::jmin(680, area.getHeight() - 96));
}

SettingsWindow::~SettingsWindow() {
    clearContentComponent();
}

void SettingsWindow::closeButtonPressed() {
    if (panel_ != nullptr)
        panel_->windowClosing();
    setVisible(false);
    if (openWindow == this)
        openWindow = nullptr;
    closingWindow = this;
    juce::MessageManager::callAsync([self = juce::Component::SafePointer<SettingsWindow>(this)] {
        delete self.getComponent();
    });
}

void SettingsWindow::show(SettingsSectionId section, const juce::String& tab) {
    if (openWindow == nullptr) {
        openWindow = new SettingsWindow();
        openWindow->setVisible(true);
    }
    openWindow->panel_->open(section, tab);
    openWindow->toFront(true);
}

void SettingsWindow::setContext(juce::ApplicationCommandManager* commandManager,
                                AudioEngine* audioEngine) {
    contextCommandManager = commandManager;
    contextAudioEngine = audioEngine;
}

void SettingsWindow::closeIfOpen() {
    if (openWindow != nullptr) {
        openWindow->panel_->windowClosing();
        delete openWindow.getComponent();
    }
    delete closingWindow.getComponent();
}

}  // namespace magda
