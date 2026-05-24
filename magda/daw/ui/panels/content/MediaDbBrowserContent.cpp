#include "MediaDbBrowserContent.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <unordered_map>

#include "../../../audio/AudioBridge.hpp"
#include "../../../core/ClipManager.hpp"
#include "../../../core/TrackManager.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../../media_db/MediaDatabase.hpp"
#include "../../../media_db/MediaDbContext.hpp"
#include "../../../media_db/MediaDbIndexer.hpp"
#include "../../../media_db/MediaDbMetadata.hpp"
#include "../../../media_db/SampleTaggerDownloader.hpp"
#include "../../components/chain/layout/DeviceSlotHeaderLayout.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FileBrowserLookAndFeel.hpp"
#include "../../themes/FontManager.hpp"
#include "../../themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

// ---- Column IDs ---------------------------------------------------------
//
// TableHeaderComponent identifies columns by ID, not by index, so reorders
// don't change these. Defined as constants for the model's paintCell switch.

enum ColumnId {
    kColName = 1,
    kColFamily = 2,
    kColShape = 3,
    kColBpm = 4,
    kColKey = 5,
    kColDuration = 6,
    kColTags = 7,
    kColStatus = 8,  // file-integrity badge: missing / dirty / ok
};

// File-integrity classification for a row's backing file. Computed lazily
// on the first paint of the Status cell and cached by file_id so we don't
// re-stat on every redraw.
enum class RowIntegrity {
    Unknown = 0,  // not yet computed; renders blank
    Ok,           // file exists, size + mtime match indexed values
    Dirty,        // file exists but size or mtime differ from index
    Missing,      // file doesn't exist on disk
};

// ---- Filter combo value tables -----------------------------------------
//
// Indexed by combo selectedId: 1 == "no filter" (sentinel), 2+ == the
// matching column value. Hard-coded so we don't depend on ComboBox's
// getItemText/getSelectedItemIndex timing inside onChange — which has
// produced "first selection doesn't take" behaviour in the past.

const std::vector<juce::String> kFamilies = {
    "",  // id=1 sentinel
    "drum", "bass", "lead", "pad", "keys", "guitar", "orchestral", "vocal", "fx", "texture",
};

const std::vector<juce::String> kShapes = {
    "",  // id=1 sentinel
    "one-shot",
    "loop",
    "sustained",
};

// Matches AudioFeatures.cpp::kPitchClasses (PathRules normalises flats → sharps).
const std::vector<juce::String> kKeys = {
    "",  // id=1 sentinel
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

std::optional<std::string> selectedString(const juce::ComboBox& cb,
                                          const std::vector<juce::String>& table) {
    const int id = cb.getSelectedId();
    if (id <= 1 || id - 1 >= static_cast<int>(table.size())) {
        return std::nullopt;
    }
    return table[static_cast<size_t>(id - 1)].toStdString();
}

class IndexTagOptionsComponent final : public juce::Component {
  public:
    IndexTagOptionsComponent() {
        addAndMakeVisible(includeRootFolderName_);
        addAndMakeVisible(includePathNodes_);

        includeRootFolderName_.setToggleState(true, juce::dontSendNotification);
        includePathNodes_.setToggleState(false, juce::dontSendNotification);

        setSize(280, 58);
    }

    bool includeRootFolderName() const {
        return includeRootFolderName_.getToggleState();
    }

    bool includePathNodes() const {
        return includePathNodes_.getToggleState();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        includeRootFolderName_.setBounds(bounds.removeFromTop(24));
        bounds.removeFromTop(6);
        includePathNodes_.setBounds(bounds.removeFromTop(24));
    }

  private:
    juce::ToggleButton includeRootFolderName_{"Use folder name"};
    juce::ToggleButton includePathNodes_{"Use subfolder names"};
};

juce::String prettyDuration(std::optional<double> seconds) {
    if (!seconds) {
        return "-";
    }
    const double s = *seconds;
    if (s < 1.0) {
        return juce::String(s, 2) + "s";
    }
    return juce::String(s, 1) + "s";
}

juce::String displayNameFor(const magda::media::QueryResult& result) {
    if (result.displayName && !result.displayName->empty()) {
        return juce::String(*result.displayName);
    }
    return juce::String(result.path.filename().string());
}

// A compact, branded chip used as the drag image for preset rows. macOS would
// otherwise show the generic blank-document icon for an unregistered .mps; this
// gives a clear, distinctive snapshot naming the preset(s) being dragged.
juce::Image makePresetDragImage(const juce::StringArray& names) {
    const juce::String text =
        names.size() == 1 ? names[0] : (juce::String(names.size()) + " presets");
    auto font = FontManager::getInstance().getUIFont(12.0F);
    const int textW = juce::GlyphArrangement::getStringWidthInt(font, text);
    const int padLeft = 26;  // room for the glyph
    const int padRight = 12;
    const int width = juce::jlimit(90, 260, padLeft + textW + padRight);
    const int height = 26;

    juce::Image img(juce::Image::ARGB, width, height, true);
    juce::Graphics g(img);

    auto bounds =
        juce::Rectangle<float>(0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height))
            .reduced(0.5F);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.96F));
    g.fillRoundedRectangle(bounds, 6.0F);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.drawRoundedRectangle(bounds, 6.0F, 1.5F);

    // Two offset squares read as a stacked "preset".
    const float gy = static_cast<float>(height) * 0.5F;
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.fillRoundedRectangle(8.0F, gy - 6.0F, 8.0F, 8.0F, 2.0F);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE).withAlpha(0.96F));
    g.fillRoundedRectangle(11.0F, gy - 2.5F, 8.0F, 8.0F, 2.0F);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    g.drawRoundedRectangle(11.0F, gy - 2.5F, 8.0F, 8.0F, 2.0F, 1.0F);

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    g.setFont(font);
    g.drawText(text, juce::Rectangle<int>(padLeft, 0, width - padLeft - padRight, height),
               juce::Justification::centredLeft, true);
    return img;
}

std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    return path.lexically_normal();
}

std::optional<magda::ClipId> findOpenClipForPath(const std::filesystem::path& path) {
    const auto target = normalizedPath(path);
    for (const auto& clip : magda::ClipManager::getInstance().getClips()) {
        juce::String sourcePath;
        if (clip.isAudio()) {
            sourcePath = clip.audio().source.filePath;
        } else if (clip.isMidi()) {
            sourcePath = clip.midi().sourceFilePath;
        }
        if (sourcePath.isEmpty()) {
            continue;
        }
        if (normalizedPath(std::filesystem::path(sourcePath.toStdString())) == target) {
            return clip.id;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<magda::ClipInfo::WarpMarker>> currentWarpMarkersForClip(
    magda::ClipId clipId) {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || !clip->isAudio() || !clip->warpEnabled) {
        return std::nullopt;
    }

    std::vector<magda::ClipInfo::WarpMarker> markers;
    if (auto* engine = magda::TrackManager::getInstance().getAudioEngine()) {
        if (auto* bridge = engine->getAudioBridge()) {
            const auto liveMarkers = bridge->getWarpMarkers(clipId);
            markers.reserve(liveMarkers.size());
            for (const auto& marker : liveMarkers) {
                markers.push_back({marker.sourceTime, marker.warpTime});
            }
        }
    }
    if (markers.empty()) {
        markers = clip->warpMarkers;
    }
    return markers;
}

juce::String formatIndexSummary(const std::filesystem::path& path,
                                const magda::media::MediaDbIndexer::Stats& stats) {
    const int total = stats.inserted + stats.updated + stats.skipped + stats.failed;
    juce::String summary = juce::String("Scan complete: ") + juce::String(total) + " files, " +
                           juce::String(stats.inserted) + " inserted, " +
                           juce::String(stats.updated) + " updated, " +
                           juce::String(stats.skipped) + " skipped";
    if (stats.failed > 0) {
        summary += ", " + juce::String(stats.failed) + " failed";
    }
    summary += " - " + juce::String(path.filename().string());
    return summary;
}

juce::String formatAnalysisSummary(const magda::media::MediaDbIndexer::EmbeddingStats& stats) {
    return juce::String("Analysis: ") + juce::String(stats.embedded) + " files analyzed, " +
           juce::String(stats.failed) + " failed";
}

juce::String formatAnalysisSummary(const magda::media::MediaDbIndexer::Stats& decodeStats,
                                   const magda::media::MediaDbIndexer::EmbeddingStats& tagStats) {
    return juce::String("Analysis complete: ") + juce::String(decodeStats.inserted) +
           " inserted, " + juce::String(decodeStats.updated) + " updated, " +
           juce::String(tagStats.embedded) + " analyzed, " +
           juce::String(decodeStats.failed + tagStats.failed) + " failed";
}

juce::String formatEta(std::chrono::steady_clock::duration remaining) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
    if (seconds < 1) {
        return "<1s";
    }

    const auto hours = seconds / 3600;
    seconds %= 3600;
    const auto minutes = seconds / 60;
    seconds %= 60;

    if (hours > 0) {
        return juce::String(static_cast<int>(hours)) + "h " +
               juce::String(static_cast<int>(minutes)) + "m";
    }
    if (minutes > 0) {
        return juce::String(static_cast<int>(minutes)) + "m " +
               juce::String(static_cast<int>(seconds)) + "s";
    }
    return juce::String(static_cast<int>(seconds)) + "s";
}

juce::String formatAnalysisProgress(int done, int total, const std::filesystem::path& current,
                                    std::chrono::steady_clock::time_point startedAt) {
    juce::String status = juce::String("Analyzing ") + juce::String(current.filename().string()) +
                          " (" + juce::String(done) + "/" + juce::String(total) + ")";
    if (done > 0 && total > done) {
        const auto elapsed = std::chrono::steady_clock::now() - startedAt;
        const auto perFile = elapsed / done;
        status += " - ETA " + formatEta(perFile * (total - done));
    }
    return status;
}

juce::String prettyBpm(std::optional<double> bpm) {
    if (!bpm) {
        return "-";
    }
    return juce::String(static_cast<int>(std::round(*bpm)));
}

juce::String prettyKey(const std::optional<std::string>& root,
                       const std::optional<std::string>& scale) {
    if (!root) {
        return "-";
    }
    juce::String out(*root);
    if (scale && !scale->empty()) {
        out += " " + juce::String(*scale).substring(0, 3);  // "maj" / "min"
    }
    return out;
}

bool isAllowedValue(const juce::String& value, const std::vector<juce::String>& values) {
    for (size_t i = 1; i < values.size(); ++i) {
        if (value.equalsIgnoreCase(values[i])) {
            return true;
        }
    }
    return value.equalsIgnoreCase("unknown");
}

std::optional<double> parseOptionalPositiveDouble(const juce::String& raw, bool& ok) {
    const auto text = raw.trim();
    if (text.isEmpty() || text == "-") {
        return std::nullopt;
    }
    int dots = 0;
    for (int i = 0; i < text.length(); ++i) {
        const auto c = text[i];
        if (c == '.') {
            ++dots;
        } else if (!juce::CharacterFunctions::isDigit(c)) {
            ok = false;
            return std::nullopt;
        }
    }
    if (dots > 1) {
        ok = false;
        return std::nullopt;
    }
    return text.getDoubleValue();
}

std::vector<std::string> parseTags(const juce::String& raw) {
    juce::StringArray tokens;
    tokens.addTokens(raw, ", \t\r\n", "\"'");
    tokens.trim();
    tokens.removeEmptyStrings();

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(tokens.size()));
    for (const auto& token : tokens) {
        const auto clean = token.trim().toLowerCase();
        if (clean.isNotEmpty()) {
            const auto s = clean.toStdString();
            if (std::find(out.begin(), out.end(), s) == out.end()) {
                out.push_back(s);
            }
        }
    }
    return out;
}

std::optional<std::vector<std::string>> parseOptionalTags(const juce::String& raw) {
    const auto text = raw.trim();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    if (text == "-") {
        return std::vector<std::string>{};
    }
    return parseTags(text);
}

juce::String joinTags(const std::vector<std::string>& tags) {
    juce::String out;
    for (const auto& tag : tags) {
        if (out.isNotEmpty()) {
            out += ", ";
        }
        out += juce::String(tag);
    }
    return out;
}

class MediaDbTableHeader : public juce::TableHeaderComponent {
  public:
    std::function<void()> onRemoveDuplicateRows;

    void addMenuItems(juce::PopupMenu& menu, int columnIdClicked) override {
        juce::TableHeaderComponent::addMenuItems(menu, columnIdClicked);
        menu.addSeparator();
        menu.addItem(kRemoveDuplicateRowsId, "Remove duplicate file rows...");
    }

    void reactToMenuItem(int menuReturnId, int columnIdClicked) override {
        if (menuReturnId == kRemoveDuplicateRowsId) {
            if (onRemoveDuplicateRows) {
                onRemoveDuplicateRows();
            }
            return;
        }
        juce::TableHeaderComponent::reactToMenuItem(menuReturnId, columnIdClicked);
    }

  private:
    static constexpr int kRemoveDuplicateRowsId = 0x4D445250;  // MDRP
};

}  // namespace

// ===========================================================================
// ResultsTableModel — paints one cell of the TableListBox per column
// ===========================================================================

class MediaDbBrowserContent::ResultsTableModel : public juce::TableListBoxModel {
  public:
    explicit ResultsTableModel(MediaDbBrowserContent& owner) : owner_(owner) {}

    int getNumRows() override {
        return static_cast<int>(owner_.results_.size());
    }

    // Discard cached integrity entries. Call after results_ is replaced
    // (new query, new page) or after a re-index — both can leave stale
    // entries from previous file_ids or pre-change mtime/size values.
    void clearIntegrityCache() {
        integrityCache_.clear();
    }

    RowIntegrity integrityFor(const magda::media::QueryResult& r) {
        if (auto it = integrityCache_.find(r.fileId); it != integrityCache_.end()) {
            return it->second;
        }
        RowIntegrity state = RowIntegrity::Ok;
        std::error_code ec;
        if (!std::filesystem::exists(r.path, ec) || ec) {
            state = RowIntegrity::Missing;
        } else {
            const auto sz = std::filesystem::file_size(r.path, ec);
            if (ec) {
                state = RowIntegrity::Missing;
            } else {
                const auto mt = std::filesystem::last_write_time(r.path, ec);
                if (ec) {
                    state = RowIntegrity::Missing;
                } else {
                    const auto curMtimeNs =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(mt.time_since_epoch())
                            .count();
                    if (static_cast<std::int64_t>(sz) != r.sizeBytes || curMtimeNs != r.mtimeNs) {
                        state = RowIntegrity::Dirty;
                    }
                }
            }
        }
        integrityCache_.emplace(r.fileId, state);
        return state;
    }

    void sortOrderChanged(int newSortColumnId, bool isForwards) override {
        owner_.setSortOrder(newSortColumnId, isForwards);
    }

    void paintRowBackground(juce::Graphics& g, int rowNumber, int /*width*/, int /*height*/,
                            bool rowIsSelected) override {
        if (rowIsSelected) {
            g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE_HOVER));
            return;
        }
        if (rowNumber >= 0 && rowNumber < static_cast<int>(owner_.results_.size())) {
            const auto& r = owner_.results_[static_cast<size_t>(rowNumber)];
            if (r.kind == "audio" && r.tagged) {
                g.fillAll(DarkTheme::getAccentColour().withAlpha(0.055F));
            }
        }
    }

    void paintCell(juce::Graphics& g, int row, int columnId, int width, int height,
                   bool /*rowIsSelected*/) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& r = owner_.results_[static_cast<size_t>(row)];
        const auto cell = juce::Rectangle<int>(0, 0, width, height);

        const auto& font = FontManager::getInstance().getUIFont(11.0F);
        g.setFont(font);

        auto drawPill = [&](const juce::String& text, juce::Colour col, bool showMenuArrow) {
            if (text.isEmpty()) {
                return;
            }
            auto pill = cell.reduced(6, 4);
            g.setColour(col.withAlpha(0.15F));
            g.fillRoundedRectangle(pill.toFloat(), 3.0F);
            g.setColour(col);
            g.drawRoundedRectangle(pill.toFloat(), 3.0F, 1.0F);
            auto textArea = pill;
            if (showMenuArrow) {
                textArea.removeFromRight(12);
            }
            g.drawText(text, textArea, juce::Justification::centred, true);
            if (showMenuArrow) {
                const auto arrow = pill.removeFromRight(12).toFloat();
                juce::Path p;
                p.addTriangle(arrow.getCentreX() - 3.0F, arrow.getCentreY() - 1.5F,
                              arrow.getCentreX() + 3.0F, arrow.getCentreY() - 1.5F,
                              arrow.getCentreX(), arrow.getCentreY() + 3.0F);
                g.fillPath(p);
            }
        };

        switch (columnId) {
            case kColName: {
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                if (r.userEdited) {
                    const float dotR = 3.0F;
                    g.setColour(DarkTheme::getAccentColour());
                    g.fillEllipse(8.0F, static_cast<float>(height) * 0.5F - dotR, dotR * 2.0F,
                                  dotR * 2.0F);
                }
                const bool rowMissing = integrityFor(r) == RowIntegrity::Missing;
                if (rowMissing) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.55F));
                    g.setFont(font.italicised());
                } else if (r.kind == "audio" && !r.tagged) {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                    g.setFont(font.italicised());
                } else {
                    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
                }
                g.drawText(displayNameFor(r), cell.withTrimmedLeft(18).reduced(0, 2),
                           juce::Justification::centredLeft, true);
                break;
            }
            case kColStatus: {
                // Three states. Ok renders blank to keep healthy rows quiet.
                // Dirty: amber outlined circle. Missing: filled red circle
                // with a small slash through it. Same colours as elsewhere
                // in the app so it matches the existing visual vocabulary.
                const auto state = integrityFor(r);
                if (state == RowIntegrity::Ok || state == RowIntegrity::Unknown) {
                    break;
                }
                const float r2 = 4.0F;
                const auto cx = static_cast<float>(width) * 0.5F;
                const auto cy = static_cast<float>(height) * 0.5F;
                const auto rect = juce::Rectangle<float>(cx - r2, cy - r2, r2 * 2.0F, r2 * 2.0F);
                if (state == RowIntegrity::Dirty) {
                    g.setColour(juce::Colour(0xFFE0A93D));
                    g.drawEllipse(rect, 1.5F);
                } else {  // Missing
                    g.setColour(juce::Colour(0xFFD05A4A));
                    g.fillEllipse(rect);
                    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE_HOVER));
                    g.drawLine(cx - r2 * 0.6F, cy + r2 * 0.6F, cx + r2 * 0.6F, cy - r2 * 0.6F,
                               1.4F);
                }
                break;
            }
            case kColFamily:
                drawPill(juce::String(r.family), DarkTheme::getColour(DarkTheme::ACCENT_BLUE),
                         true);
                break;
            case kColShape:
                drawPill(juce::String(r.shape), DarkTheme::getColour(DarkTheme::TEXT_PRIMARY),
                         true);
                break;
            case kColBpm:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(prettyBpm(r.bpm), cell.reduced(6, 2), juce::Justification::centredRight,
                           true);
                break;
            case kColKey:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(prettyKey(r.keyRoot, r.keyScale), cell.reduced(6, 2),
                           juce::Justification::centredRight, true);
                break;
            case kColDuration:
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                g.drawText(prettyDuration(r.durationS), cell.reduced(6, 2),
                           juce::Justification::centredRight, true);
                break;
            case kColTags: {
                // Comma-joined tag list, single-line, truncated at the cell
                // edge. Hidden by default in the docked browser; visible in
                // the pop-out window where there's more horizontal room.
                g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
                juce::String joined;
                for (const auto& t : r.tags) {
                    if (joined.isNotEmpty()) {
                        joined += ", ";
                    }
                    joined += juce::String(t);
                }
                g.drawText(joined, cell.reduced(6, 2), juce::Justification::centredLeft, true);
                break;
            }
            default:
                break;
        }
    }

    void cellClicked(int row, int columnId, const juce::MouseEvent& e) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& r = owner_.results_[static_cast<size_t>(row)];

        // Pill-click bulk semantics: if the clicked row is part of a
        // multi-row selection, apply the chosen value to every selected
        // row. Otherwise just the clicked row. Use the pre-click snapshot
        // because a plain click already collapsed getSelectedRows() down
        // to the clicked row by the time we run.
        struct PillTargets {
            std::vector<std::int64_t> ids;
            juce::SparseSet<int> rows;
            bool restoreSelection = false;
        };

        auto pillTargets = [&]() {
            PillTargets targets;
            std::vector<std::int64_t> ids;
            const auto& sel = owner_.resultsTable_.preClickSelection_;
            bool clickedInSelection = false;
            for (int i = 0; i < sel.size(); ++i) {
                const int sr = sel[i];
                if (sr < 0 || sr >= static_cast<int>(owner_.results_.size())) {
                    continue;
                }
                if (sr == row) {
                    clickedInSelection = true;
                }
                ids.push_back(owner_.results_[static_cast<size_t>(sr)].fileId);
                targets.rows.addRange(juce::Range<int>(sr, sr + 1));
            }
            if (!clickedInSelection || ids.size() < 2) {
                targets.ids.push_back(r.fileId);
                targets.rows.clear();
                targets.rows.addRange(juce::Range<int>(row, row + 1));
                return targets;
            }
            targets.ids = std::move(ids);
            targets.restoreSelection = true;
            return targets;
        };

        if (columnId == kColFamily && e.mods.isLeftButtonDown()) {
            auto targets = pillTargets();
            if (targets.restoreSelection) {
                owner_.resultsTable_.setSelectedRows(targets.rows, juce::sendNotification);
            }
            owner_.showFamilyMenuForRow(std::move(targets.ids), juce::String(r.family),
                                        e.getScreenPosition());
            return;
        }
        if (columnId == kColShape && e.mods.isLeftButtonDown()) {
            auto targets = pillTargets();
            if (targets.restoreSelection) {
                owner_.resultsTable_.setSelectedRows(targets.rows, juce::sendNotification);
            }
            owner_.showShapeMenuForRow(std::move(targets.ids), juce::String(r.shape),
                                       e.getScreenPosition());
            return;
        }

        // Right-click: context menu. Left/middle: preview / select.
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            const auto fileName = displayNameFor(r);
            std::vector<std::int64_t> selectedIds;
            const auto selectedRows = owner_.resultsTable_.getSelectedRows();
            for (int i = 0; i < selectedRows.size(); ++i) {
                const int selectedRow = selectedRows[i];
                if (selectedRow >= 0 && selectedRow < static_cast<int>(owner_.results_.size())) {
                    selectedIds.push_back(owner_.results_[static_cast<size_t>(selectedRow)].fileId);
                }
            }
            if (selectedIds.empty()) {
                selectedIds.push_back(r.fileId);
            }
            const int selectedCount = static_cast<int>(selectedIds.size());
            const bool rowMissing = integrityFor(r) == RowIntegrity::Missing;
            const bool hasMatchingClip = findOpenClipForPath(r.path).has_value();
            menu.addItem(4, selectedCount > 1 ? "Edit selected row fields and tags..."
                                              : "Edit row fields and tags...");
            menu.addItem(
                6, selectedCount > 1 ? "Reset selected rows to detected" : "Reset to detected",
                !owner_.indexing_);
            menu.addItem(7, "Save current clip values to library",
                         !owner_.indexing_ && selectedCount == 1 && hasMatchingClip);
            menu.addItem(8, "Recover missing file...",
                         !owner_.indexing_ && selectedCount == 1 && rowMissing);
            menu.addItem(5, "Delete selected rows (" + juce::String(selectedCount) + ")",
                         !owner_.indexing_);
            menu.addSeparator();
            menu.addItem(1, "Find similar sounds to \"" + fileName + "\"");
            menu.addSeparator();
            menu.addItem(2, "Analyze selected rows (" + juce::String(selectedCount) + ")");
            const juce::Component::SafePointer<MediaDbBrowserContent> self(&owner_);
            const auto seedId = r.fileId;
            const auto seedName = fileName;
            menu.showMenuAsync(
                juce::PopupMenu::Options{},
                [self, seedId, seedName, selectedIds = std::move(selectedIds)](int choice) mutable {
                    if (self == nullptr) {
                        return;
                    }
                    if (choice == 1) {
                        self->findSimilarTo(seedId, seedName);
                    } else if (choice == 2) {
                        self->startAnalyzingFileIds(std::move(selectedIds));
                    } else if (choice == 4) {
                        if (selectedIds.size() > 1) {
                            self->showBulkEditRowsDialog(std::move(selectedIds));
                        } else {
                            self->showEditRowDialog(seedId);
                        }
                    } else if (choice == 5) {
                        self->deleteFileIdsWithConfirmation(std::move(selectedIds));
                    } else if (choice == 6) {
                        self->resetRowsToDetected(std::move(selectedIds));
                    } else if (choice == 7) {
                        self->saveMatchingClipValuesToLibrary(seedId);
                    } else if (choice == 8) {
                        self->recoverMissingFile(seedId);
                    }
                });
            return;
        }

        if (owner_.onFileSelected) {
            owner_.onFileSelected(juce::File(juce::String(r.path.string())));
        }
    }

    void cellDoubleClicked(int row, int columnId, const juce::MouseEvent& e) override {
        if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
            return;
        }
        const auto& result = owner_.results_[static_cast<size_t>(row)];
        if (columnId == kColFamily) {
            owner_.showFamilyMenuForRow({result.fileId}, juce::String(result.family),
                                        e.getScreenPosition());
            return;
        }
        if (columnId == kColShape) {
            owner_.showShapeMenuForRow({result.fileId}, juce::String(result.shape),
                                       e.getScreenPosition());
            return;
        }
        owner_.showEditRowDialog(result.fileId);
    }

    // Kicks off a file drag for the selected rows.
    //
    // On macOS/Windows we run an OS-level drag so the payload reaches
    // FileDragAndDropTarget consumers (TrackContentPanel, SessionView,
    // Finder, plugin file slots). Deferred via callAsync because
    // performExternalDragDropOfFiles enters a modal native run loop and
    // we're sitting inside ListBox's mouseDrag callback — bouncing
    // through the message queue gets us out of that nested call.
    //
    // On Linux we return a {type:"files",paths:[...]} description so JUCE
    // drives a non-modal internal drag instead. JUCE has no Wayland DnD
    // and same-app external drags are unreliable even on X11, so OS DnD
    // silently fails. TrackContentPanel::itemDropped recognises this
    // payload. Tradeoff: drops into Finder / FileDragAndDropTarget-only
    // consumers are not supported via this route.
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override {
        if (owner_.dragInProgress_ || selectedRows.isEmpty()) {
            return {};
        }
        juce::StringArray paths;
        juce::StringArray presetNames;
        bool allPresets = true;
        for (int i = 0; i < selectedRows.size(); ++i) {
            const int row = selectedRows[i];
            if (row < 0 || row >= static_cast<int>(owner_.results_.size())) {
                continue;
            }
            const auto& result = owner_.results_[static_cast<size_t>(row)];
            paths.addIfNotAlreadyThere(juce::String(result.path.string()));
            if (result.kind == "preset") {
                presetNames.add(displayNameFor(result));
            } else {
                allPresets = false;
            }
        }
        if (paths.isEmpty()) {
            return {};
        }

        // Preset rows: drive a JUCE-internal drag with a clean, branded image on
        // every platform. The in-app preset drop targets (track FX chain,
        // arrangement) recognise the {type:"files"} payload, and presets aren't
        // dragged out to Finder, so the OS file-drag route isn't needed here.
        // This also avoids macOS showing the generic blank-document .mps icon.
        if (allPresets) {
            juce::Array<juce::var> pathArray;
            for (const auto& p : paths)
                pathArray.add(p);
            auto* obj = new juce::DynamicObject();
            obj->setProperty("type", juce::var("files"));
            obj->setProperty("paths", juce::var(pathArray));
            juce::var description(obj);
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(&owner_)) {
                container->startDragging(description, &owner_,
                                         juce::ScaledImage(makePresetDragImage(presetNames)));
            }
            return {};  // we started the drag ourselves; suppress ListBox's default
        }
#if JUCE_LINUX
        juce::Array<juce::var> pathArray;
        for (const auto& p : paths)
            pathArray.add(p);
        auto* obj = new juce::DynamicObject();
        obj->setProperty("type", juce::var("files"));
        obj->setProperty("paths", juce::var(pathArray));
        return juce::var(obj);  // ListBox will run startDragging with this payload
#else
        owner_.dragInProgress_ = true;
        const juce::Component::SafePointer<MediaDbBrowserContent> src(&owner_);
        juce::MessageManager::callAsync([paths, src]() {
            if (src == nullptr) {
                return;
            }
            juce::DragAndDropContainer::performExternalDragDropOfFiles(paths,
                                                                       /*canMoveFiles=*/false, src);
            src->dragInProgress_ = false;
        });
        return {};  // suppress JUCE's internal drag — we handle it ourselves
#endif
    }

  private:
    MediaDbBrowserContent& owner_;
    std::unordered_map<std::int64_t, RowIntegrity> integrityCache_;
};

// ===========================================================================
// MediaDbBrowserContent
// ===========================================================================

MediaDbBrowserContent::MediaDbBrowserContent(bool isPopOutInstance)
    : isPopOutInstance_(isPopOutInstance) {
    // Style mirrors PluginBrowserContent's compact filter strip:
    // SmallComboBoxLookAndFeel on dropdowns + explicit theme colours on
    // each control. Labels and the index button share FileBrowserLookAndFeel
    // because it carries the matching theme-font overrides.
    auto& comboLnf = SmallComboBoxLookAndFeel::getInstance();
    auto& fbLnf = FileBrowserLookAndFeel::getInstance();
    const auto uiFont = FontManager::getInstance().getUIFont(11.0F);

    bpmLabel_.setText("bpm", juce::dontSendNotification);
    bpmLabel_.setFont(uiFont);
    bpmLabel_.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    bpmLabel_.setJustificationType(juce::Justification::centredRight);

    auto styleCombo = [&](juce::ComboBox& cb) {
        cb.setColour(juce::ComboBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        cb.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        cb.setColour(juce::ComboBox::outlineColourId, DarkTheme::getBorderColour());
        cb.setLookAndFeel(&comboLnf);
    };

    // Family — strict closed-set dropdown (same shape as key/shape). The
    // tags free-text field below is for matching anything that doesn't fit
    // the predefined families.
    familyFilter_.addItem("family: all", 1);
    for (size_t i = 1; i < kFamilies.size(); ++i) {
        familyFilter_.addItem(kFamilies[i], static_cast<int>(i + 1));
    }
    familyFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(familyFilter_);
    familyFilter_.onChange = [this]() { restartSearch(); };

    shapeFilter_.addItem("shape: any", 1);
    for (size_t i = 1; i < kShapes.size(); ++i) {
        shapeFilter_.addItem(kShapes[i], static_cast<int>(i + 1));
    }
    shapeFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(shapeFilter_);
    shapeFilter_.onChange = [this]() { restartSearch(); };

    keyFilter_.addItem("key: any", 1);
    for (size_t i = 1; i < kKeys.size(); ++i) {
        keyFilter_.addItem(kKeys[i], static_cast<int>(i + 1));
    }
    keyFilter_.setSelectedId(1, juce::dontSendNotification);
    styleCombo(keyFilter_);
    keyFilter_.onChange = [this]() { restartSearch(); };

    auto setupBpm = [&](juce::TextEditor& e, const juce::String& placeholder) {
        e.setTextToShowWhenEmpty(placeholder, DarkTheme::getSecondaryTextColour());
        e.setColour(juce::TextEditor::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
        e.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
        e.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
        e.setInputRestrictions(4, "0123456789.");
        e.setFont(uiFont);
        e.onReturnKey = [this]() { restartSearch(); };
        e.onFocusLost = [this]() { restartSearch(); };
    };
    setupBpm(bpmMinBox_, "min");
    setupBpm(bpmMaxBox_, "max");

    tonalOnly_.setLookAndFeel(&fbLnf);
    tonalOnly_.onClick = [this]() { restartSearch(); };

    // Tags free-text filter — whitespace-separated tokens are AND-combined
    // via FTS5 MATCH against media_fts.tag_text. Updates on Enter / blur,
    // matching the BPM range editors so we don't query on every keystroke.
    tagsFilter_.setTextToShowWhenEmpty("tags (e.g. drum 808)", DarkTheme::getSecondaryTextColour());
    tagsFilter_.setColour(juce::TextEditor::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    tagsFilter_.setColour(juce::TextEditor::textColourId, DarkTheme::getTextColour());
    tagsFilter_.setColour(juce::TextEditor::outlineColourId, DarkTheme::getBorderColour());
    tagsFilter_.setFont(uiFont);
    tagsFilter_.onReturnKey = [this]() { restartSearch(); };
    tagsFilter_.onFocusLost = [this]() { restartSearch(); };

    // Pop-out button — opens this view in its own DocumentWindow. Hidden in
    // the pop-out instance to avoid recursive windows.
    if (!isPopOutInstance_) {
        popOutButton_ = std::make_unique<magda::SvgButton>(
            "Open in window", BinaryData::open_in_new_svg, BinaryData::open_in_new_svgSize);
        popOutButton_->setTooltip("Open in a separate window");
        popOutButton_->onClick = [this]() { openPopOutWindow(); };
        addAndMakeVisible(*popOutButton_);
    }

    addAndMakeVisible(familyFilter_);
    addAndMakeVisible(shapeFilter_);
    addAndMakeVisible(keyFilter_);
    addAndMakeVisible(bpmLabel_);
    addAndMakeVisible(bpmMinBox_);
    addAndMakeVisible(bpmMaxBox_);
    addAndMakeVisible(tonalOnly_);
    addAndMakeVisible(tagsFilter_);

    // Kind selection lives outside this component — see setKindFilter().
    // The search-bar file-type icons in MediaExplorerContent double as the
    // kind selector when library mode is active, so we don't duplicate them
    // here.

    // Results table — drag-out is wired via
    // ResultsTableModel::getDragSourceDescription (encodes the row paths
    // in the drag var) + MediaDbBrowserContent::shouldDropFilesWhenDraggedExternally
    // (decodes them and asks the OS to drop them as files when the drag
    // leaves the app window).
    auto tableHeader = std::make_unique<MediaDbTableHeader>();
    tableHeader->onRemoveDuplicateRows = [this]() { removeDuplicateFilePathsWithConfirmation(); };
    resultsTable_.setHeader(std::move(tableHeader));
    resultsModel_ = std::make_unique<ResultsTableModel>(*this);
    resultsTable_.setModel(resultsModel_.get());
    resultsTable_.setRowHeight(28);
    resultsTable_.setMultipleSelectionEnabled(true);
    resultsTable_.setHeaderHeight(22);
    resultsTable_.setColour(juce::ListBox::backgroundColourId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND));
    resultsTable_.setColour(juce::ListBox::outlineColourId, DarkTheme::getBorderColour());
    resultsTable_.setOutlineThickness(1);

    auto& header = resultsTable_.getHeader();
    header.setColour(juce::TableHeaderComponent::backgroundColourId,
                     DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05F));
    header.setColour(juce::TableHeaderComponent::textColourId,
                     DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    header.setColour(juce::TableHeaderComponent::outlineColourId, DarkTheme::getBorderColour());
    // appearsOnColumnMenu — right-click the header to show/hide any column.
    // JUCE drives the visibility toggle itself once the flag is set.
    const int flags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable |
                      juce::TableHeaderComponent::draggable |
                      juce::TableHeaderComponent::appearsOnColumnMenu |
                      juce::TableHeaderComponent::sortable;
    // (id, name, defaultWidth, minWidth, maxWidth, propertyFlags)
    header.addColumn("Name", kColName, 260, 80, -1, flags);
    // Narrow icon-only file-integrity column. No header label — the badge
    // legend lives in tooltip / docs. Not sortable; status is computed
    // client-side and a SQL sort key would require schema changes.
    header.addColumn({}, kColStatus, 22, 22, 22,
                     juce::TableHeaderComponent::visible | juce::TableHeaderComponent::draggable |
                         juce::TableHeaderComponent::appearsOnColumnMenu);
    header.addColumn("Family", kColFamily, 90, 50, -1, flags);
    header.addColumn("Shape", kColShape, 90, 50, -1, flags);
    header.addColumn("BPM", kColBpm, 60, 40, -1, flags);
    header.addColumn("Key", kColKey, 60, 40, -1, flags);
    header.addColumn("Duration", kColDuration, 70, 40, -1, flags);
    header.addColumn("Tags", kColTags, 200, 80, -1, flags);

    // Per-view default visibility: the docked browser stays compact, the
    // pop-out window (the "extended editor") shows tags. Users can override
    // either way via the column-header right-click menu — JUCE persists
    // the choice for the lifetime of the table instance.
    header.setColumnVisible(kColTags, isPopOutInstance_);
    header.setStretchToFitActive(true);  // expand columns to fill width

    addAndMakeVisible(resultsTable_);

    // Empty state
    emptyState_.setText("No samples in your library yet.\nRight-click a folder in the browser "
                        "and choose \"Index this folder\".",
                        juce::dontSendNotification);
    emptyState_.setFont(FontManager::getInstance().getUIFont(13.0F));
    emptyState_.setJustificationType(juce::Justification::centred);
    emptyState_.setColour(juce::Label::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    emptyState_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(emptyState_);

    // Pagination footer. Prev / "Page N" label / Next. Hidden when the
    // result set fits in a single page (no need for any chrome). Uses the
    // shared makeNavArrowButton style so it matches device/param paging
    // arrows elsewhere in the app.
    prevPageBtn_ = makeNavArrowButton("prev", 0.5F);
    prevPageBtn_->onClick = [this]() {
        if (currentPage_ > 0) {
            --currentPage_;
            runSearch();
        }
    };
    nextPageBtn_ = makeNavArrowButton("next", 0.0F);
    nextPageBtn_->onClick = [this]() {
        ++currentPage_;
        runSearch();
    };
    pageLabel_.setFont(FontManager::getInstance().getUIFont(11.0F));
    pageLabel_.setJustificationType(juce::Justification::centred);
    pageLabel_.setColour(juce::Label::textColourId,
                         DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    pageLabel_.setInterceptsMouseClicks(false, false);
    prevPageBtn_->setVisible(false);
    nextPageBtn_->setVisible(false);
    pageLabel_.setVisible(false);
    addAndMakeVisible(*prevPageBtn_);
    addAndMakeVisible(pageLabel_);
    addAndMakeVisible(*nextPageBtn_);

    // Similar-mode status. Indexing status is forwarded to MediaExplorerContent's
    // shared bottom strip so it is visible in one place only.
    statusLabel_.setFont(FontManager::getInstance().getUIFont(10.0F));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::textColourId,
                           DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    statusLabel_.setMinimumHorizontalScale(1.0F);  // truncate long paths, don't shrink the font
    statusLabel_.setInterceptsMouseClicks(false, false);
    statusLabel_.setVisible(false);
    addAndMakeVisible(statusLabel_);

    // Defer the first query until the library icon is actually clicked.
    // Opening SQLite + loading CLAP models can take seconds; doing it during
    // app startup would freeze the splash. setQueryText()/refresh() trigger
    // runSearch() lazily once the user enters library mode.
    observedMediaRevision_ = magda::media::MediaDbContext::getInstance().mediaRevision();
    startTimerHz(4);
}

MediaDbBrowserContent::~MediaDbBrowserContent() {
    stopTimer();
    // Drain in-flight indexing jobs. removeAllJobs(true, ...) signals
    // cancellation and waits up to the timeout for the worker to exit.
    if (indexCancel_) {
        indexCancel_->store(true);
    }
    if (indexPool_) {
        indexPool_->removeAllJobs(true, 5000);
    }
    if (searchPool_) {
        searchPool_->removeAllJobs(true, 5000);
    }
    resultsTable_.setModel(nullptr);
    familyFilter_.setLookAndFeel(nullptr);
    shapeFilter_.setLookAndFeel(nullptr);
    keyFilter_.setLookAndFeel(nullptr);
    bpmMinBox_.setLookAndFeel(nullptr);
    bpmMaxBox_.setLookAndFeel(nullptr);
    tonalOnly_.setLookAndFeel(nullptr);
}

void MediaDbBrowserContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::SURFACE));
}

void MediaDbBrowserContent::resized() {
    auto bounds = getLocalBounds();

    // Two-row filter strip. Row 1: family / shape / key dropdowns + pop-out.
    // Row 2: BPM range + tonal. Kind selection is driven externally from the
    // search-bar icons (see MediaExplorerContent), so no third row.
    auto row1 = bounds.removeFromTop(28);
    row1.removeFromLeft(4);
    row1.removeFromRight(4);
    if (popOutButton_) {
        popOutButton_->setBounds(row1.removeFromRight(24).reduced(2));
        row1.removeFromRight(6);
    }
    familyFilter_.setBounds(row1.removeFromLeft(110).reduced(2));
    row1.removeFromLeft(6);
    shapeFilter_.setBounds(row1.removeFromLeft(100).reduced(2));
    row1.removeFromLeft(6);
    keyFilter_.setBounds(row1.removeFromLeft(90).reduced(2));

    auto row2 = bounds.removeFromTop(28);
    row2.removeFromLeft(4);
    row2.removeFromRight(4);
    bpmLabel_.setBounds(row2.removeFromLeft(30));
    bpmMinBox_.setBounds(row2.removeFromLeft(60).reduced(2));
    row2.removeFromLeft(4);
    bpmMaxBox_.setBounds(row2.removeFromLeft(60).reduced(2));
    row2.removeFromLeft(8);
    tonalOnly_.setBounds(row2.removeFromLeft(70).reduced(2));
    row2.removeFromLeft(8);
    // Tags fills the rest of the row — gives it room to breathe in
    // popped-out / wide panels and stays at minimum ~140px in narrow ones.
    tagsFilter_.setBounds(row2.reduced(2));

    bounds.removeFromTop(4);

    // Similar-mode status strip at the bottom of the content area.
    if (statusLabel_.isVisible()) {
        statusLabel_.setBounds(bounds.removeFromBottom(18).reduced(8, 2));
    } else {
        statusLabel_.setBounds(bounds.getX() + 8, bounds.getBottom() - 18, bounds.getWidth() - 16,
                               18);
    }

    // Pagination footer sits just above the status strip: < | Page N | >.
    // Hidden when there's only one page worth of results -> no reserved space.
    if (prevPageBtn_ != nullptr && prevPageBtn_->isVisible()) {
        auto pager = bounds.removeFromBottom(20).reduced(8, 2);
        placeNavArrow(*prevPageBtn_, pager, /*fromLeft=*/true);
        placeNavArrow(*nextPageBtn_, pager, /*fromLeft=*/false);
        pageLabel_.setBounds(pager);
    }

    resultsTable_.setBounds(bounds);
    emptyState_.setBounds(bounds);
}

void MediaDbBrowserContent::setQueryText(const juce::String& text) {
    if (queryText_ == text) {
        return;
    }
    queryText_ = text;
    restartSearch();
}

void MediaDbBrowserContent::refresh() {
    restartSearch();
}

void MediaDbBrowserContent::timerCallback() {
    const auto revision = magda::media::MediaDbContext::getInstance().mediaRevision();
    if (revision == observedMediaRevision_) {
        return;
    }
    observedMediaRevision_ = revision;
    if (isShowing()) {
        runSearch();
    }
}

void MediaDbBrowserContent::restartSearch() {
    currentPage_ = 0;
    similarToFileId_.reset();
    similarToFileName_.clear();
    runSearch();
}

magda::media::QuerySort MediaDbBrowserContent::currentSort() const {
    return sort_;
}

void MediaDbBrowserContent::setSortOrder(int columnId, bool ascending) {
    using magda::media::QuerySortField;
    QuerySortField field = QuerySortField::Default;
    switch (columnId) {
        case kColName:
            field = QuerySortField::Name;
            break;
        case kColFamily:
            field = QuerySortField::Family;
            break;
        case kColShape:
            field = QuerySortField::Shape;
            break;
        case kColBpm:
            field = QuerySortField::Bpm;
            break;
        case kColKey:
            field = QuerySortField::Key;
            break;
        case kColDuration:
            field = QuerySortField::Duration;
            break;
        case kColTags:
            field = QuerySortField::Tags;
            break;
        default:
            field = QuerySortField::Default;
            break;
    }
    sort_ = {field, ascending};
    currentPage_ = 0;
    runSearch();
}

void MediaDbBrowserContent::findSimilarTo(std::int64_t seedFileId, const juce::String& seedName) {
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (ctx.ensureInitialized()) {
        magda::media::MediaDbQuery probe(ctx.db(), nullptr, nullptr);
        if (!probe.hasEmbedding(seedFileId)) {
            // No CLAP vector for this file — most likely it was indexed
            // before the Sample Analyzer bundle was installed, so cosine
            // similarity is impossible. Tell the user instead of silently
            // returning an empty result list.
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions{}
                    .withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Find similar sounds")
                    .withMessage("\"" + seedName +
                                 "\" has not been analyzed for similarity search yet.\n"
                                 "\n"
                                 "This usually means the file was indexed before the Sample "
                                 "Analyzer bundle was installed. Right-click the row and choose "
                                 "\"Analyze selected rows\".")
                    .withButton("OK"),
                nullptr);
            return;
        }
    }
    similarToFileId_ = seedFileId;
    similarToFileName_ = seedName;
    currentPage_ = 0;
    runSearch();
}

void MediaDbBrowserContent::showEditRowDialog(std::int64_t fileId) {
    if (indexing_) {
        return;
    }
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }
    auto row = magda::media::getEditableMediaRow(ctx.db(), fileId);
    if (!row) {
        return;
    }

    auto* alert = new juce::AlertWindow(
        "Edit Media Row", "Changes update the media database row, not the file on disk.",
        juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("display", row->displayName ? juce::String(*row->displayName) : "",
                         "Display name:");
    alert->addTextEditor("family", juce::String(row->family), "Family:");
    alert->addTextEditor("shape", juce::String(row->shape), "Shape:");
    alert->addTextEditor("bpm", row->bpm ? juce::String(*row->bpm, 2) : "", "BPM:");
    alert->addTextEditor("key_root", row->keyRoot ? juce::String(*row->keyRoot) : "", "Key root:");
    alert->addTextEditor("key_scale", row->keyScale ? juce::String(*row->keyScale) : "",
                         "Key scale:");
    alert->addTextEditor("duration", row->durationS ? juce::String(*row->durationS, 3) : "",
                         "Duration (s):");
    alert->addTextEditor("tags", joinTags(row->tags), "Tags:");
    alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, self,
                                                   row = std::move(*row)](int result) mutable {
            if (result != 1) {
                delete alert;
                return;
            }

            auto edited = row;
            const auto display = alert->getTextEditorContents("display").trim();
            const auto family = alert->getTextEditorContents("family").trim().toLowerCase();
            const auto shape = alert->getTextEditorContents("shape").trim().toLowerCase();
            const auto keyRoot = alert->getTextEditorContents("key_root").trim();
            const auto keyScale = alert->getTextEditorContents("key_scale").trim().toLowerCase();
            bool ok = true;
            edited.bpm = parseOptionalPositiveDouble(alert->getTextEditorContents("bpm"), ok);
            edited.durationS =
                parseOptionalPositiveDouble(alert->getTextEditorContents("duration"), ok);
            edited.tags = parseTags(alert->getTextEditorContents("tags"));
            delete alert;

            if (self == nullptr) {
                return;
            }
            if (!ok || !isAllowedValue(family, kFamilies) || !isAllowedValue(shape, kShapes)) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Edit Media Row")
                        .withMessage("Family, shape, BPM, or duration contains an "
                                     "unsupported value.")
                        .withButton("OK"),
                    nullptr);
                return;
            }

            edited.displayName = display.isEmpty()
                                     ? std::optional<std::string>{}
                                     : std::optional<std::string>{display.toStdString()};
            edited.family = family.isEmpty() ? "unknown" : family.toStdString();
            edited.shape = shape.isEmpty() ? "unknown" : shape.toStdString();
            edited.keyRoot = keyRoot.isEmpty() ? std::optional<std::string>{}
                                               : std::optional<std::string>{keyRoot.toStdString()};
            edited.keyScale = keyScale.isEmpty()
                                  ? std::optional<std::string>{}
                                  : std::optional<std::string>{keyScale.toStdString()};

            auto& ctx = magda::media::MediaDbContext::getInstance();
            const bool saved =
                ctx.ensureInitialized() && magda::media::updateEditableMediaRow(ctx.db(), edited);
            if (!saved) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Edit Media Row")
                        .withMessage("Could not save the database row.")
                        .withButton("OK"),
                    nullptr);
                return;
            }
            self->restartSearch();
        }));
}

void MediaDbBrowserContent::showBulkEditRowsDialog(std::vector<std::int64_t> fileIds) {
    if (indexing_ || fileIds.empty()) {
        return;
    }
    const auto count = static_cast<int>(fileIds.size());
    auto* alert = new juce::AlertWindow(
        "Edit Selected Rows",
        "Blank fields keep each row's current value. Enter \"-\" in Tags to clear tags.",
        juce::MessageBoxIconType::NoIcon);
    alert->addTextEditor("family", "", "Family:");
    alert->addTextEditor("shape", "", "Shape:");
    alert->addTextEditor("bpm", "", "BPM:");
    alert->addTextEditor("key_root", "", "Key root:");
    alert->addTextEditor("key_scale", "", "Key scale:");
    alert->addTextEditor("duration", "", "Duration (s):");
    alert->addTextEditor("tags", "", "Tags:");
    alert->addButton("Apply to " + juce::String(count), 1,
                     juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([alert, self,
                                                   ids = std::move(fileIds)](int result) mutable {
            if (result != 1) {
                delete alert;
                return;
            }

            const auto family = alert->getTextEditorContents("family").trim().toLowerCase();
            const auto shape = alert->getTextEditorContents("shape").trim().toLowerCase();
            const auto keyRoot = alert->getTextEditorContents("key_root").trim();
            const auto keyScale = alert->getTextEditorContents("key_scale").trim().toLowerCase();
            bool ok = true;

            magda::media::BulkEditableMediaUpdate update;
            if (family.isNotEmpty()) {
                update.family = family.toStdString();
            }
            if (shape.isNotEmpty()) {
                update.shape = shape.toStdString();
            }
            if (keyRoot.isNotEmpty()) {
                update.keyRoot = keyRoot.toStdString();
            }
            if (keyScale.isNotEmpty()) {
                update.keyScale = keyScale.toStdString();
            }
            update.bpm = parseOptionalPositiveDouble(alert->getTextEditorContents("bpm"), ok);
            update.durationS =
                parseOptionalPositiveDouble(alert->getTextEditorContents("duration"), ok);
            update.tags = parseOptionalTags(alert->getTextEditorContents("tags"));
            delete alert;

            if (self == nullptr) {
                return;
            }
            if (!ok || (update.family && !isAllowedValue(family, kFamilies)) ||
                (update.shape && !isAllowedValue(shape, kShapes))) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Edit Selected Rows")
                        .withMessage("Family, shape, BPM, or duration contains an unsupported "
                                     "value.")
                        .withButton("OK"),
                    nullptr);
                return;
            }

            auto& ctx = magda::media::MediaDbContext::getInstance();
            const int updated = ctx.ensureInitialized()
                                    ? magda::media::updateEditableMediaRows(ctx.db(), ids, update)
                                    : 0;
            if (updated <= 0) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Edit Selected Rows")
                        .withMessage("No rows were updated.")
                        .withButton("OK"),
                    nullptr);
                return;
            }
            self->restartSearch();
        }));
}

void MediaDbBrowserContent::resetRowsToDetected(std::vector<std::int64_t> fileIds) {
    if (indexing_ || fileIds.empty()) {
        return;
    }

    auto& ctx = magda::media::MediaDbContext::getInstance();
    const int updated =
        ctx.ensureInitialized() ? magda::media::resetMediaRowsToDetected(ctx.db(), fileIds) : 0;
    if (updated <= 0) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle("Reset to detected")
                                         .withMessage("No media rows were reset.")
                                         .withButton("OK"),
                                     nullptr);
        return;
    }
    ctx.bumpMediaRevision();
    restartSearch();
}

void MediaDbBrowserContent::saveMatchingClipValuesToLibrary(std::int64_t fileId) {
    if (indexing_) {
        return;
    }
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }
    auto row = magda::media::getEditableMediaRow(ctx.db(), fileId);
    if (!row) {
        return;
    }
    auto clipId = findOpenClipForPath(row->path);
    if (!clipId) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                         .withIconType(juce::MessageBoxIconType::InfoIcon)
                                         .withTitle("Save current clip values")
                                         .withMessage("No open clip is using this media file.")
                                         .withButton("OK"),
                                     nullptr);
        return;
    }

    const auto markers = currentWarpMarkersForClip(*clipId);
    const bool saved = magda::ClipManager::getInstance().saveClipToLibrary(*clipId, markers);
    if (!saved) {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions{}
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Save current clip values")
                .withMessage("Could not save the matching clip values to the media library.")
                .withButton("OK"),
            nullptr);
        return;
    }
    restartSearch();
}

void MediaDbBrowserContent::recoverMissingFile(std::int64_t fileId) {
    if (indexing_) {
        return;
    }
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        return;
    }

    auto candidates = magda::media::findMissingFileCandidates(ctx.db(), fileId);
    if (candidates.empty()) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions{}
                                         .withIconType(juce::MessageBoxIconType::InfoIcon)
                                         .withTitle("Recover Missing File")
                                         .withMessage("No indexed substitute files were found.")
                                         .withButton("OK"),
                                     nullptr);
        return;
    }

    juce::PopupMenu menu;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        const auto label = juce::String(candidate.matchReason) + ": " +
                           juce::String(candidate.path.filename().string()) + " - " +
                           juce::String(candidate.path.parent_path().string());
        menu.addItem(static_cast<int>(i + 1), label);
    }

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options{},
        [self, fileId, candidates = std::move(candidates)](int choice) mutable {
            if (self == nullptr || choice <= 0 || choice > static_cast<int>(candidates.size())) {
                return;
            }

            auto& ctx = magda::media::MediaDbContext::getInstance();
            const auto& candidate = candidates[static_cast<size_t>(choice - 1)];
            const bool recovered =
                ctx.ensureInitialized() &&
                magda::media::recoverMissingMediaFilePath(ctx.db(), fileId, candidate.path);
            if (!recovered) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Recover Missing File")
                        .withMessage("Could not update the media database path.")
                        .withButton("OK"),
                    nullptr);
                return;
            }
            ctx.bumpMediaRevision();
            self->restartSearch();
        });
}

void MediaDbBrowserContent::showFamilyMenuForRow(std::vector<std::int64_t> fileIds,
                                                 const juce::String& currentFamily,
                                                 juce::Point<int> screenPosition) {
    if (indexing_ || fileIds.empty()) {
        return;
    }

    juce::PopupMenu menu;
    menu.addItem(1, "unknown", true, currentFamily.equalsIgnoreCase("unknown"));
    for (size_t i = 1; i < kFamilies.size(); ++i) {
        const auto& family = kFamilies[i];
        menu.addItem(static_cast<int>(i + 1), family, true, currentFamily.equalsIgnoreCase(family));
    }

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    const auto target = juce::Rectangle<int>(screenPosition.x, screenPosition.y, 1, 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(target),
                       [self, fileIds = std::move(fileIds)](int choice) {
                           if (choice <= 0 || self == nullptr) {
                               return;
                           }

                           juce::String selected = "unknown";
                           if (choice > 1) {
                               const auto index = static_cast<size_t>(choice - 1);
                               if (index >= kFamilies.size()) {
                                   return;
                               }
                               selected = kFamilies[index];
                           }

                           magda::media::BulkEditableMediaUpdate update;
                           update.family = selected.toStdString();
                           auto& ctx = magda::media::MediaDbContext::getInstance();
                           const int updated = ctx.ensureInitialized()
                                                   ? magda::media::updateEditableMediaRows(
                                                         ctx.db(), fileIds, update)
                                                   : 0;
                           if (updated <= 0) {
                               juce::AlertWindow::showAsync(
                                   juce::MessageBoxOptions{}
                                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                                       .withTitle("Family")
                                       .withMessage("Could not update the media row(s).")
                                       .withButton("OK"),
                                   nullptr);
                               return;
                           }
                           self->runSearch();
                       });
}

void MediaDbBrowserContent::showShapeMenuForRow(std::vector<std::int64_t> fileIds,
                                                const juce::String& currentShape,
                                                juce::Point<int> screenPosition) {
    if (indexing_ || fileIds.empty()) {
        return;
    }

    juce::PopupMenu menu;
    menu.addItem(1, "unknown", true, currentShape.equalsIgnoreCase("unknown"));
    for (size_t i = 1; i < kShapes.size(); ++i) {
        const auto& shape = kShapes[i];
        menu.addItem(static_cast<int>(i + 1), shape, true, currentShape.equalsIgnoreCase(shape));
    }

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    const auto target = juce::Rectangle<int>(screenPosition.x, screenPosition.y, 1, 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(target),
                       [self, fileIds = std::move(fileIds)](int choice) {
                           if (choice <= 0 || self == nullptr) {
                               return;
                           }

                           juce::String selected = "unknown";
                           if (choice > 1) {
                               const auto index = static_cast<size_t>(choice - 1);
                               if (index >= kShapes.size()) {
                                   return;
                               }
                               selected = kShapes[index];
                           }

                           magda::media::BulkEditableMediaUpdate update;
                           update.shape = selected.toStdString();
                           auto& ctx = magda::media::MediaDbContext::getInstance();
                           const int updated = ctx.ensureInitialized()
                                                   ? magda::media::updateEditableMediaRows(
                                                         ctx.db(), fileIds, update)
                                                   : 0;
                           if (updated <= 0) {
                               juce::AlertWindow::showAsync(
                                   juce::MessageBoxOptions{}
                                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                                       .withTitle("Shape")
                                       .withMessage("Could not update the media row(s).")
                                       .withButton("OK"),
                                   nullptr);
                               return;
                           }
                           self->runSearch();
                       });
}

void MediaDbBrowserContent::deleteFileIdsWithConfirmation(std::vector<std::int64_t> fileIds) {
    if (indexing_ || fileIds.empty()) {
        return;
    }
    const auto count = static_cast<int>(fileIds.size());
    const auto message = count == 1 ? juce::String("Remove this row from the media database?")
                                    : juce::String("Remove ") + juce::String(count) +
                                          " rows from the media database?";
    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions{}
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Delete Media Rows")
            .withMessage(message + "\n\nFiles on disk will not be deleted.")
            .withButton("Delete")
            .withButton("Cancel"),
        [self, ids = std::move(fileIds)](int result) mutable {
            if (result != 1 || self == nullptr) {
                return;
            }
            auto& ctx = magda::media::MediaDbContext::getInstance();
            const int removed =
                ctx.ensureInitialized() ? magda::media::deleteMediaRows(ctx.db(), ids) : 0;
            if (removed <= 0) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions{}
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Delete Media Rows")
                        .withMessage("No rows were removed.")
                        .withButton("OK"),
                    nullptr);
                return;
            }
            self->restartSearch();
        });
}

void MediaDbBrowserContent::removeDuplicateFilePathsWithConfirmation() {
    if (indexing_) {
        return;
    }

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions{}
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Remove Duplicate Rows")
            .withMessage(
                "Remove database rows that point to the same physical file?\n\n"
                "MAGDA keeps the row with user edits first, then analyzed rows, then the newest "
                "indexed row. Files on disk will not be deleted.")
            .withButton("Remove")
            .withButton("Cancel"),
        [self](int result) {
            if (result != 1 || self == nullptr) {
                return;
            }

            if (!self->indexPool_) {
                self->indexPool_ = std::make_unique<juce::ThreadPool>(1);
            }
            self->indexing_ = true;
            if (self->onIndexingActiveChanged) {
                self->onIndexingActiveChanged(true);
            }
            if (self->onIndexingStatus) {
                self->onIndexingStatus("Removing duplicate file rows...");
            }

            self->indexPool_->addJob([self]() {
                int removed = 0;
                juce::String status;
                try {
                    magda::media::MediaDatabase bgDb(
                        magda::media::MediaDbContext::getInstance().dbPath());
                    removed = magda::media::removeDuplicateFilePathRows(bgDb);
                    status = removed > 0 ? juce::String("Removed ") + juce::String(removed) +
                                               " duplicate file row" + (removed == 1 ? "" : "s")
                                         : "No duplicate file rows found";
                } catch (const std::exception& e) {
                    status = juce::String("Remove duplicates failed: ") + juce::String(e.what());
                }

                juce::MessageManager::callAsync([self, status, removed]() {
                    if (self == nullptr) {
                        return;
                    }
                    self->indexing_ = false;
                    if (self->onIndexingActiveChanged) {
                        self->onIndexingActiveChanged(false);
                    }
                    if (removed > 0) {
                        magda::media::MediaDbContext::getInstance().bumpMediaRevision();
                    }
                    if (self->onIndexingStatus) {
                        self->onIndexingStatus(status);
                    }
                    self->restartSearch();
                });
            });
        });
}

void MediaDbBrowserContent::requestStopIndexing() {
    if (indexCancel_) {
        indexCancel_->store(true);
    }
    if (onIndexingStatus) {
        onIndexingStatus("Stopping...");
    }
}

void MediaDbBrowserContent::startAnalyzingFileIds(std::vector<std::int64_t> fileIds) {
    if (indexing_ || fileIds.empty()) {
        return;
    }
    if (!indexPool_) {
        indexPool_ = std::make_unique<juce::ThreadPool>(1);
    }

    indexing_ = true;
    indexCancel_ = std::make_shared<std::atomic_bool>(false);
    if (onIndexingActiveChanged) {
        onIndexingActiveChanged(true);
    }
    const auto prepText =
        "Preparing analysis for " + juce::String(static_cast<int>(fileIds.size())) + " rows...";
    if (onIndexingStatus) {
        onIndexingStatus(prepText);
    }

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    auto cancelToken = indexCancel_;
    indexPool_->addJob([self, ids = std::move(fileIds), cancelToken]() {
        juce::String finalStatus;
        try {
            magda::media::MediaDatabase bgDb(magda::media::MediaDbContext::getInstance().dbPath());
            auto& ctx = magda::media::MediaDbContext::getInstance();
            auto* encoder = ctx.audioEncoder();
            // The indexer writes CLAP zero-shot tags during the embedding
            // pass when the text encoder + tokenizer are available (issue
            // #1319). Pass providers rather than already-loaded pointers so
            // the ~480 MB text-model load is deferred until the indexer
            // actually has pending audio rows to embed.
            magda::media::MediaDbIndexer indexer(
                bgDb, encoder,
                []() { return magda::media::MediaDbContext::getInstance().textEncoder(); },
                []() { return magda::media::MediaDbContext::getInstance().tokenizer(); });
            indexer.setShouldCancel([cancelToken]() { return cancelToken && cancelToken->load(); });
            indexer.setFailureCallback(
                [](const std::filesystem::path& failedPath, const std::string& reason) {
                    juce::Logger::writeToLog(juce::String("[MediaDbIndexer] Failed analysis: ") +
                                             juce::String(failedPath.string()) + " - " +
                                             juce::String(reason));
                });
            indexer.setProgress([self](int done, int total, const std::filesystem::path& current) {
                const auto status = juce::String("Analyzing ") +
                                    juce::String(current.filename().string()) + " (" +
                                    juce::String(done) + "/" + juce::String(total) + ")";
                juce::MessageManager::callAsync([self, status]() {
                    if (self != nullptr) {
                        if (self->onIndexingStatus) {
                            self->onIndexingStatus(status);
                        }
                    }
                });
            });
            const auto decodeStats =
                indexer.indexFileIds(ids, magda::media::MediaDbIndexer::Mode::ForceAll);

            magda::media::MediaDbIndexer::EmbeddingStats tagStats;
            if (encoder != nullptr && !(cancelToken && cancelToken->load())) {
                const auto analysisStartedAt = std::chrono::steady_clock::now();
                indexer.setProgress([self, analysisStartedAt](
                                        int done, int total, const std::filesystem::path& current) {
                    const auto status =
                        formatAnalysisProgress(done, total, current, analysisStartedAt);
                    juce::MessageManager::callAsync([self, status]() {
                        if (self != nullptr) {
                            if (self->onIndexingStatus) {
                                self->onIndexingStatus(status);
                            }
                        }
                    });
                });
                tagStats = indexer.embedAudioFileIds(ids);
            }

            finalStatus = formatAnalysisSummary(decodeStats, tagStats);
            if (cancelToken && cancelToken->load()) {
                finalStatus = "Analysis stopped: " + finalStatus;
            }
        } catch (const std::exception& e) {
            finalStatus = juce::String("Analysis failed: ") + juce::String(e.what());
        }

        juce::MessageManager::callAsync([self, finalStatus, cancelToken]() {
            if (self == nullptr) {
                return;
            }
            self->indexing_ = false;
            if (self->indexCancel_ == cancelToken) {
                self->indexCancel_.reset();
            }
            if (self->onIndexingActiveChanged) {
                self->onIndexingActiveChanged(false);
            }
            if (self->onIndexingStatus) {
                self->onIndexingStatus(finalStatus);
            }
            self->restartSearch();
        });
    });
}

magda::media::QueryFilters MediaDbBrowserContent::currentFilters() const {
    magda::media::QueryFilters f;
    // Read filter values from a hard-coded table indexed by selectedId
    // instead of asking the ComboBox for the displayed text — getText() and
    // getItemText(getSelectedItemIndex()) have both been seen to lag the
    // selectedId update on first interaction, causing a stale or empty
    // filter on the first call to onChange.
    // Family / shape / key are strict closed-set dropdowns — read by
    // selectedId so onChange always sees a coherent value (getText() can
    // briefly lag).
    f.family = selectedString(familyFilter_, kFamilies);
    f.shape = selectedString(shapeFilter_, kShapes);
    f.keyRoot = selectedString(keyFilter_, kKeys);

    if (kindFilter_) {
        f.kind = *kindFilter_;
    }

    if (bpmMinBox_.getText().isNotEmpty()) {
        f.bpmMin = bpmMinBox_.getText().getDoubleValue();
    }
    if (bpmMaxBox_.getText().isNotEmpty()) {
        f.bpmMax = bpmMaxBox_.getText().getDoubleValue();
    }
    if (tonalOnly_.getToggleState()) {
        f.tonal = true;
    }
    const auto tagsText = tagsFilter_.getText().trim();
    if (tagsText.isNotEmpty()) {
        f.tags = tagsText.toStdString();
    }
    return f;
}

void MediaDbBrowserContent::runSearch() {
    auto& ctx = magda::media::MediaDbContext::getInstance();
    if (!ctx.ensureInitialized()) {
        results_.clear();
        resultsTable_.updateContent();
        resultsTable_.repaint();
        emptyState_.setText("Failed to open the media database.", juce::dontSendNotification);
        emptyState_.setVisible(true);
        resultsTable_.setVisible(false);
        return;
    }

    const auto filters = currentFilters();
    const auto sort = currentSort();
    const bool hasText = !queryText_.isEmpty();
    const int offset = currentPage_ * kPageSize;

    // Similar-sounds mode. No text encoder needed — uses the seed's audio
    // embedding already in the DB. Cosine over up to kCandidateCap=5000
    // candidate embeddings takes tens of ms, so route to the worker.
    if (similarToFileId_.has_value()) {
        const auto seedId = *similarToFileId_;
        const int myGen = ++searchGeneration_;
        results_.clear();
        resultsTable_.updateContent();
        resultsTable_.repaint();
        resultsTable_.setVisible(false);
        emptyState_.setText("Finding similar sounds...", juce::dontSendNotification);
        emptyState_.setVisible(true);

        if (!searchPool_) {
            searchPool_ = std::make_unique<juce::ThreadPool>(1);
        }
        const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
        searchPool_->addJob([self, myGen, seedId, filters, offset, sort]() {
            std::vector<magda::media::QueryResult> results;
            try {
                if (self == nullptr) {
                    return;
                }
                auto& bgCtx = magda::media::MediaDbContext::getInstance();
                if (!self->searchDb_) {
                    self->searchDb_ = std::make_unique<magda::media::MediaDatabase>(bgCtx.dbPath());
                }
                magda::media::MediaDbQuery query(*self->searchDb_, nullptr, nullptr);
                results = query.similarTo(seedId, filters, kPageSize, offset, sort);
            } catch (...) {
                // Empty result -> UI shows the "No results" empty state.
            }
            juce::MessageManager::callAsync([self, myGen, r = std::move(results)]() mutable {
                if (self == nullptr) {
                    return;
                }
                if (self->searchGeneration_.load() != myGen) {
                    return;
                }
                self->results_ = std::move(r);
                self->applySearchResultsToUi();
            });
        });
        return;
    }

    // Filter-only fast path — pure SQL on indexed columns, runs in
    // microseconds. Keep it synchronous so the table updates without an
    // async hop.
    if (!hasText) {
        magda::media::MediaDbQuery query(ctx.db(), nullptr, nullptr);
        results_ =
            query.search(std::nullopt, filters, /*limit=*/kPageSize, /*offset=*/offset, {}, sort);
        applySearchResultsToUi();
        return;
    }

    // Text-query slow path. The ONNX text encoder lazy-loads (~480 MB) on
    // first call and runs inference per query — hundreds of milliseconds
    // to multi-second. Run on a background thread and gate stale results
    // with searchGeneration_ so a rapid sequence of keystrokes doesn't
    // overwrite the latest result with an older worker's output.
    const int myGen = ++searchGeneration_;
    const auto text = queryText_.toStdString();

    // Loading state on the UI while the worker runs. If the text encoder
    // isn't loaded yet the worker will trigger its lazy-load — that's a
    // multi-second ONNX session build, so we surface it explicitly rather
    // than calling it "Searching..." and looking hung.
    results_.clear();
    resultsTable_.updateContent();
    resultsTable_.repaint();
    resultsTable_.setVisible(false);
    const bool needsModelLoad = magda::media::SampleTaggerDownloader::isInstalled() &&
                                (!ctx.isTextEncoderLoaded() || !ctx.isTokenizerLoaded());
    emptyState_.setText(needsModelLoad ? "Loading text-search model (~500 MB)..." : "Searching...",
                        juce::dontSendNotification);
    emptyState_.setVisible(true);

    if (!searchPool_) {
        searchPool_ = std::make_unique<juce::ThreadPool>(1);
    }

    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    searchPool_->addJob([self, myGen, text, filters, offset, sort]() {
        std::vector<magda::media::QueryResult> results;
        try {
            if (self == nullptr) {
                return;
            }
            // Lazily build a worker-owned SQLite connection on first run
            // and reuse it across queries. SQLite multi-thread mode allows
            // one connection per thread; the search pool is single-threaded
            // so the same worker keeps touching this handle. WAL lets it
            // coexist with the UI thread's connection.
            auto& ctx = magda::media::MediaDbContext::getInstance();
            if (!self->searchDb_) {
                self->searchDb_ = std::make_unique<magda::media::MediaDatabase>(ctx.dbPath());
            }
            // First call from a worker also pays the lazy-load cost
            // (multi-second). Subsequent calls are cheap; ORT Session::Run
            // is documented thread-safe so a shared encoder pointer is OK.
            auto* textEnc = ctx.textEncoder();
            auto* tok = ctx.tokenizer();
            magda::media::MediaDbQuery query(*self->searchDb_, textEnc, tok);
            results = query.search(std::optional<std::string>{text}, filters, kPageSize, offset, {},
                                   sort);
        } catch (...) {
            // Swallow — empty result set bounces back to the UI either way.
        }

        juce::MessageManager::callAsync([self, myGen, r = std::move(results)]() mutable {
            if (self == nullptr) {
                return;
            }
            if (self->searchGeneration_.load() != myGen) {
                return;  // newer query in flight, drop this stale result
            }
            self->results_ = std::move(r);
            self->applySearchResultsToUi();
        });
    });
}

void MediaDbBrowserContent::applySearchResultsToUi() {
    // updateContent() refreshes the row count; explicit repaint forces the
    // paint region invalidation (we've seen this matter on TableListBox).
    if (resultsModel_) {
        resultsModel_->clearIntegrityCache();
    }
    resultsTable_.updateContent();
    resultsTable_.syncSelectionSnapshot();
    resultsTable_.repaint();

    const bool empty = results_.empty();
    resultsTable_.setVisible(!empty);
    emptyState_.setVisible(empty);
    if (empty) {
        // Probe library state once — used by every empty-state branch
        // below to decide between "library is empty", "filters excluded
        // everything", and "library has no analysis data for similarity".
        int libraryRows = -1;
        int embeddingRows = -1;
        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (ctx.ensureInitialized()) {
            magda::media::MediaDbQuery probe(ctx.db(), nullptr, nullptr);
            libraryRows = probe.totalFiles();
            embeddingRows = probe.totalEmbeddings();
        }

        juce::String text;
        if (libraryRows == 0) {
            // Library is genuinely empty — overrides every other state.
            text = "No samples in your library yet.\nRight-click a folder in the browser "
                   "and choose \"Index this folder\".";
        } else if (similarToFileId_.has_value()) {
            text = "No similar sounds found for \"" + similarToFileName_ + "\".";
            if (embeddingRows <= 1) {
                text += "\n\nThe library contains " + juce::String(embeddingRows) +
                        " analyzed audio file" + (embeddingRows == 1 ? "" : "s") +
                        " total.\nRun analysis with the Sample Analyzer installed "
                        "to enable similarity search.";
            } else {
                text += "\n\nTry clearing filters - the active filter combination may exclude "
                        "all potential neighbours.";
            }
        } else {
            text = "No results match the current filters.";
            if (!queryText_.isEmpty() && !magda::media::SampleTaggerDownloader::isInstalled()) {
                text += "\n\nText search is filename / tag only without the AI Sample "
                        "Analyzer.\nInstall it from AI Settings > Sample Analyzer.";
            }
        }
        emptyState_.setText(text, juce::dontSendNotification);
    }

    // Pagination chrome.
    // - Show the footer when we're past page 1 OR the current page is full
    //   (meaning there's probably a next page).
    // - Prev disabled on page 1; Next disabled when we got a partial page.
    const bool fullPage = static_cast<int>(results_.size()) >= kPageSize;
    const bool showPager = currentPage_ > 0 || fullPage;
    prevPageBtn_->setVisible(showPager);
    nextPageBtn_->setVisible(showPager);
    pageLabel_.setVisible(showPager);
    prevPageBtn_->setEnabled(currentPage_ > 0);
    nextPageBtn_->setEnabled(fullPage);
    pageLabel_.setText("Page " + juce::String(currentPage_ + 1), juce::dontSendNotification);

    // Surface similar-mode in the status strip so the user knows why
    // they're seeing this result set (and how to leave: any filter / text
    // change clears it via restartSearch).
    if (similarToFileId_.has_value() && !indexing_) {
        statusLabel_.setText("Similar to: " + similarToFileName_ + "  -  change any filter to exit",
                             juce::dontSendNotification);
        statusLabel_.setVisible(true);
    } else if (!indexing_) {
        statusLabel_.setVisible(false);
    }

    resized();  // re-layout: the table shrinks by the footer when shown
}

void MediaDbBrowserContent::setKindFilter(std::optional<std::string> kind) {
    if (kindFilter_ == kind) {
        return;
    }
    kindFilter_ = std::move(kind);
    restartSearch();
}

void MediaDbBrowserContent::visibilityChanged() {
    // Fired the first time the sidebar puts us on screen (and every subsequent
    // show/hide). Re-running the search here guarantees the table reflects the
    // current DB state regardless of indexing that happened off-screen — and
    // sidesteps the startup ordering where setVisible(true) → refresh() →
    // resized() could populate before the table had its final bounds.
    if (!isVisible()) {
        return;
    }
    restartSearch();

    // Proactive model preload. The text encoder is ~482 MB and takes a few
    // seconds to build the ORT session; if we wait for the user's first
    // text search to trigger it, that first search feels broken. Kick off
    // preloadModels() on a worker as soon as the user enters library mode,
    // so by the time they type a query it's likely already done. No-op when
    // the bundle isn't installed (preloadModels() returns immediately) or
    // when the encoder is already loaded.
    if (magda::media::SampleTaggerDownloader::isInstalled()) {
        auto& ctx = magda::media::MediaDbContext::getInstance();
        if (!ctx.isTextEncoderLoaded() || !ctx.isTokenizerLoaded()) {
            if (!searchPool_) {
                searchPool_ = std::make_unique<juce::ThreadPool>(1);
            }
            searchPool_->addJob(
                []() { magda::media::MediaDbContext::getInstance().preloadModels(); });
        }
    }
}

void MediaDbBrowserContent::startIndexing(const juce::File& dir,
                                          magda::media::MediaDbIndexer::Mode mode) {
    if (indexing_ || !dir.isDirectory()) {
        return;
    }

    // The tag-options dialog is wrapped in a lambda so the analyzer-missing
    // warning below can gate it on the user's choice. Calling the warning
    // ahead of the tag dialog lets the user bail before filling anything
    // in, instead of being prompted twice (once for tags, once for the
    // warning).
    auto presentTagOptionsDialog = [this, dir, mode]() {
        auto* alert = new juce::AlertWindow("Index Folder",
                                            "Optional tags are written to each scanned media row.",
                                            juce::MessageBoxIconType::NoIcon);
        alert->addTextEditor("custom_tags", "", "Tags:");
        // AlertWindow::addCustomComponent does NOT take ownership (it holds a
        // plain Array<Component*>, not an OwnedArray), so this component must be
        // deleted by us alongside the alert in every callback path below.
        auto* tagOptionsComponent = new IndexTagOptionsComponent();
        alert->addCustomComponent(tagOptionsComponent);
        alert->addButton("Start", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
        alert->enterModalState(
            true, juce::ModalCallbackFunction::create([alert, tagOptionsComponent, self, dir,
                                                       mode](int result) mutable {
                if (result != 1) {
                    delete alert;
                    delete tagOptionsComponent;
                    return;
                }

                magda::media::MediaDbIndexer::ScanTagOptions options;
                options.root = std::filesystem::path(dir.getFullPathName().toStdString());
                options.customTags = parseTags(alert->getTextEditorContents("custom_tags"));
                options.includeRootFolderName = tagOptionsComponent->includeRootFolderName();
                options.includePathNodes = tagOptionsComponent->includePathNodes();
                delete alert;
                delete tagOptionsComponent;

                if (self != nullptr) {
                    self->startIndexingWithOptions(dir, mode, std::move(options));
                }
            }));
    };

    if (!magda::media::SampleTaggerDownloader::isInstalled()) {
        const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("AI Sample Analyzer not installed")
                .withMessage(
                    "Indexing will run, but without the Sample Analyzer the library can only do "
                    "filename / tag / family / BPM filtering - no semantic text search.\n\n"
                    "Install it any time from AI Settings > Sample Analyzer.")
                .withButton("Continue indexing")
                .withButton("Cancel"),
            [self, presentTagOptionsDialog = std::move(presentTagOptionsDialog)](int result) {
                if (auto* page = self.getComponent(); page != nullptr && result == 1) {
                    presentTagOptionsDialog();
                }
            });
        return;
    }
    presentTagOptionsDialog();
}

void MediaDbBrowserContent::startIndexingWithOptions(
    const juce::File& dir, magda::media::MediaDbIndexer::Mode mode,
    magda::media::MediaDbIndexer::ScanTagOptions tagOptions) {
    if (indexing_ || !dir.isDirectory()) {
        return;  // Already running, or invalid target — ignore.
    }
    // Sample-Analyzer-missing warning lives upstream in startIndexing(),
    // ahead of the tag-options dialog, so by the time we land here the
    // user has already chosen to proceed.
    runIndexing(dir, mode, std::move(tagOptions));
}

void MediaDbBrowserContent::runIndexing(const juce::File& dir,
                                        magda::media::MediaDbIndexer::Mode mode,
                                        magda::media::MediaDbIndexer::ScanTagOptions tagOptions) {
    if (indexing_ || !dir.isDirectory()) {
        return;
    }

    if (!indexPool_) {
        indexPool_ = std::make_unique<juce::ThreadPool>(1);
    }

    indexing_ = true;
    indexCancel_ = std::make_shared<std::atomic_bool>(false);
    if (onIndexingActiveChanged) {
        onIndexingActiveChanged(true);
    }
    auto prepText = [mode]() -> juce::String {
        switch (mode) {
            case magda::media::MediaDbIndexer::Mode::ForceAll:
                return "Preparing re-scan...";
            case magda::media::MediaDbIndexer::Mode::OnlyNew:
                return "Scanning for new files...";
            case magda::media::MediaDbIndexer::Mode::Incremental:
            default:
                return "Preparing scan...";
        }
    }();
    if (onIndexingStatus) {
        onIndexingStatus(prepText);
    }

    const auto path = std::filesystem::path(dir.getFullPathName().toStdString());
    const juce::Component::SafePointer<MediaDbBrowserContent> self(this);
    auto cancelToken = indexCancel_;

    indexPool_->addJob([self, path, mode, cancelToken, tagOptions = std::move(tagOptions)]() {
        // Background thread. Open a fresh DB connection here so we don't
        // share the UI-thread MediaDbContext::db() handle across threads
        // — SQLite multi-thread mode requires one connection per thread.
        // WAL mode (set in schema) lets this writer coexist with the
        // UI's reader connection.
        magda::media::MediaDbIndexer::Stats stats;
        magda::media::MediaDbIndexer::EmbeddingStats tagStats;
        juce::String finalStatus;
        bool hardFailure = false;
        std::mutex failureMutex;
        int failureCount = 0;
        std::vector<juce::String> firstFailures;

        juce::Logger::writeToLog(juce::String("[MediaDbIndexer] Starting scan: ") +
                                 juce::String(path.string()));
        try {
            magda::media::MediaDatabase bgDb(magda::media::MediaDbContext::getInstance().dbPath());
            auto& ctx = magda::media::MediaDbContext::getInstance();

            // Fire UI status on the message thread. Used for one-off
            // notifications (model loads, skip reasons) that don't fit the
            // per-file progress callback.
            auto postStatus = [self](juce::String text) {
                juce::MessageManager::callAsync([self, text]() {
                    if (self != nullptr && self->onIndexingStatus) {
                        self->onIndexingStatus(text);
                    }
                });
            };

            // audioEncoder() lazy-loads the ONNX session synchronously
            // (~5-10 s). Tell the user that's what the pause is for.
            if (!ctx.isAudioEncoderLoaded() && std::filesystem::exists(ctx.audioModelPath())) {
                postStatus("Loading Sample Tagger audio model...");
            }
            auto* encoder = ctx.audioEncoder();

            // Wrap the text encoder + tokenizer getters so the first access
            // (driven by zero-shot tagging inside the embed loop) surfaces a
            // status message before its own multi-second load.
            auto textEncoderGetter = [postStatus]() -> magda::media::ClapTextEncoder* {
                auto& c = magda::media::MediaDbContext::getInstance();
                if (!c.isTextEncoderLoaded() && std::filesystem::exists(c.textModelPath())) {
                    postStatus("Loading Sample Tagger text model...");
                }
                return c.textEncoder();
            };
            auto tokenizerGetter = [postStatus]() -> magda::media::RobertaTokenizer* {
                auto& c = magda::media::MediaDbContext::getInstance();
                if (!c.isTokenizerLoaded() && std::filesystem::exists(c.tokenizerJsonPath())) {
                    postStatus("Loading Sample Tagger tokenizer...");
                }
                return c.tokenizer();
            };
            magda::media::MediaDbIndexer indexer(bgDb, encoder, textEncoderGetter, tokenizerGetter);
            indexer.setScanTagOptions(tagOptions);
            indexer.setShouldCancel([cancelToken]() { return cancelToken && cancelToken->load(); });
            indexer.setFailureCallback([&failureMutex, &failureCount,
                                        &firstFailures](const std::filesystem::path& failedPath,
                                                        const std::string& reason) {
                const auto pathText = failedPath.empty() ? juce::String("(worker)")
                                                         : juce::String(failedPath.string());
                const auto message = juce::String("[MediaDbIndexer] Failed: ") + pathText + " - " +
                                     juce::String(reason);
                juce::Logger::writeToLog(message);

                std::lock_guard<std::mutex> lock(failureMutex);
                ++failureCount;
                if (firstFailures.size() < 5) {
                    firstFailures.push_back(message);
                }
            });
            indexer.setProgress([self, mode](int done, int total,
                                             const std::filesystem::path& current) {
                // Fired per-file on the indexer thread; marshal the text
                // update to the UI thread via callAsync.
                const char* verb = "Indexing ";
                if (mode == magda::media::MediaDbIndexer::Mode::ForceAll) {
                    verb = "Re-indexing ";
                } else if (mode == magda::media::MediaDbIndexer::Mode::OnlyNew) {
                    verb = "Scanning ";
                }
                const auto status = juce::String(verb) + juce::String(current.filename().string()) +
                                    " (" + juce::String(done) + "/" + juce::String(total) + ")";
                juce::MessageManager::callAsync([self, status]() {
                    if (self != nullptr) {
                        if (self->onIndexingStatus) {
                            self->onIndexingStatus(status);
                        }
                    }
                });
            });
            stats = indexer.indexDirectory(path, /*numThreads=*/0, mode);
            const auto scanStatus = formatIndexSummary(path, stats);
            finalStatus = scanStatus;
            juce::Logger::writeToLog(juce::String("[MediaDbIndexer] ") + scanStatus);

            const bool cancelledAfterScan = cancelToken && cancelToken->load();
            if (cancelledAfterScan) {
                finalStatus = "Scan stopped: " + scanStatus;
            }

            juce::MessageManager::callAsync([self, scanStatus, cancelledAfterScan]() {
                if (self != nullptr) {
                    const auto status = cancelledAfterScan
                                            ? juce::String("Scan stopped: ") + scanStatus
                                            : scanStatus;
                    if (self->onIndexingStatus) {
                        self->onIndexingStatus(status);
                    }
                    self->restartSearch();
                }
            });

            if (encoder != nullptr && !cancelledAfterScan) {
                const auto analysisStartedAt = std::chrono::steady_clock::now();
                indexer.setProgress([self, analysisStartedAt](
                                        int done, int total, const std::filesystem::path& current) {
                    const auto status =
                        formatAnalysisProgress(done, total, current, analysisStartedAt);
                    juce::MessageManager::callAsync([self, status]() {
                        if (self != nullptr) {
                            if (self->onIndexingStatus) {
                                self->onIndexingStatus(status);
                            }
                        }
                    });
                });
                tagStats = indexer.embedMissingAudio(path);
                const auto analysisStatus = formatAnalysisSummary(tagStats);
                const bool cancelledAfterAnalysis = cancelToken && cancelToken->load();
                finalStatus = cancelledAfterAnalysis
                                  ? scanStatus + " | Analysis stopped: " + analysisStatus
                                  : scanStatus + " | " + analysisStatus;
                juce::Logger::writeToLog(juce::String("[MediaDbIndexer] ") + analysisStatus);
            } else if (!cancelledAfterScan) {
                // Encoder unavailable — most common reason is the Sample
                // Tagger bundle isn't installed (or failed to load). Without
                // this branch the user just sees the scan summary and has
                // no idea why no audio analysis ran.
                const auto skipReason =
                    std::filesystem::exists(ctx.audioModelPath())
                        ? juce::String(
                              "Sample Tagger model failed to load — skipping audio analysis")
                        : juce::String(
                              "Sample Tagger not installed — skipping audio analysis. Install it "
                              "in AI Settings to enable semantic search.");
                finalStatus = scanStatus + " | " + skipReason;
                juce::Logger::writeToLog(juce::String("[MediaDbIndexer] ") + skipReason);
            }

            std::lock_guard<std::mutex> lock(failureMutex);
            for (const auto& failure : firstFailures) {
                juce::Logger::writeToLog(juce::String("[MediaDbIndexer] First failure: ") +
                                         failure);
            }
            if (failureCount > static_cast<int>(firstFailures.size())) {
                juce::Logger::writeToLog(
                    juce::String("[MediaDbIndexer] Additional failures: ") +
                    juce::String(failureCount - static_cast<int>(firstFailures.size())));
            }
        } catch (const std::exception& e) {
            hardFailure = true;
            finalStatus = juce::String("Scan failed: ") + juce::String(e.what());
            juce::Logger::writeToLog(juce::String("[MediaDbIndexer] ") + finalStatus);
        }

        juce::MessageManager::callAsync([self, finalStatus, hardFailure, cancelToken]() {
            if (self == nullptr) {
                return;
            }
            self->indexing_ = false;
            if (self->indexCancel_ == cancelToken) {
                self->indexCancel_.reset();
            }
            if (self->onIndexingActiveChanged) {
                self->onIndexingActiveChanged(false);
            }
            if (self->onIndexingStatus) {
                self->onIndexingStatus(finalStatus);
            }
            if (!hardFailure) {
                self->restartSearch();
            }
        });
    });
}

// ===========================================================================
// PopOutWindow — DocumentWindow that hosts a detached MediaDbBrowserContent
// ===========================================================================
//
// Self-deleting on close (the JUCE-blessed pattern for floating windows).
// The owning MediaDbBrowserContent only keeps a SafePointer to it, so when
// the user clicks the OS close button the pointer goes null and a subsequent
// pop-out click spawns a fresh window.

class MediaDbBrowserContent::PopOutWindow : public juce::DocumentWindow {
  public:
    PopOutWindow()
        : juce::DocumentWindow("MAGDA - Media Browser", DarkTheme::getColour(DarkTheme::BACKGROUND),
                               juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        // setContentOwned takes ownership and deletes on window destruction.
        setContentOwned(new MediaDbBrowserContent(/*isPopOutInstance=*/true), true);
        centreWithSize(960, 640);
        setVisible(true);
    }

    void closeButtonPressed() override {
        delete this;
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PopOutWindow)
};

void MediaDbBrowserContent::openPopOutWindow() {
    if (popOutWindow_ != nullptr) {
        // Window already exists — bring it to the front.
        popOutWindow_->toFront(true);
        return;
    }
    auto* w = new PopOutWindow();
    popOutWindow_ = w;  // SafePointer; auto-clears when w is deleted.
}

}  // namespace magda::daw::ui
