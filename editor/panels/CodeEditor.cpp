/*
 * ThermoConsole Editor — CodeEditor panel implementation
 */

#include "CodeEditor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

// ─── Lua + ThermoConsole keyword tables ─────────────────────────────────────

namespace {

const char* const LUA_KEYWORDS[] = {
    "and","break","do","else","elseif","end","false","for","function",
    "goto","if","in","local","nil","not","or","repeat","return","then",
    "true","until","while", nullptr
};

const char* const THERMO_BUILTINS[] = {
    // lifecycle
    "_init","_update","_draw",
    // graphics
    "cls","pset","pget","line","rect","rectfill","circ","circfill",
    "spr","sspr","print","camera","clip","pal","map","mget","mset","fget","mapload",
    // input
    "btn","btnp",
    // audio
    "sfx","music","stop","volume",
    // save
    "save","load","delete",
    // math
    "flr","ceil","abs","sgn","min","max","mid","sin","cos","atan2","sqrt",
    "rnd","irnd","srand",
    // system
    "time","dt","fps","stat","exit","trace","overlap","distance",
    nullptr
};

bool isKeyword(const std::string& w, const char* const* list) {
    for (int i = 0; list[i]; ++i)
        if (w == list[i]) return true;
    return false;
}

} // namespace

// ─── FileBuffer load/save ───────────────────────────────────────────────────

bool CodeEditor::FileBuffer::load() {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    // Normalize CRLF -> LF
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) if (c != '\r') out += c;
    text = std::move(out);
    modified = false;
    return true;
}

bool CodeEditor::FileBuffer::save() {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) return false;
    modified = false;
    return true;
}

// ─── CodeEditor ─────────────────────────────────────────────────────────────

CodeEditor::CodeEditor(ThermoEditor* editor) : m_editor(editor) {}

void CodeEditor::onProjectOpened(const fs::path& /*projectPath*/) {
    closeAll();
}

bool CodeEditor::anyModified() const {
    for (const auto& b : m_buffers) if (b.modified) return true;
    return false;
}

void CodeEditor::openFile(const fs::path& path) {
    // Already open? Focus the existing tab.
    for (int i = 0; i < (int)m_buffers.size(); ++i) {
        if (m_buffers[i].path == path) {
            m_pendingFocus = i;
            m_visible = true;
            return;
        }
    }
    FileBuffer buf;
    buf.path = path;
    if (!buf.load()) {
        m_editor->log("Failed to open: " + path.string(), true);
        return;
    }
    m_buffers.push_back(std::move(buf));
    m_pendingFocus = (int)m_buffers.size() - 1;
    m_visible = true;
}

void CodeEditor::saveCurrentFile() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_buffers.size()) return;
    auto& buf = m_buffers[m_activeTab];
    if (buf.save()) {
        m_editor->log("Saved: " + buf.path.filename().string());
        m_editor->notifySourceSaved();
    } else {
        m_editor->log("Save failed: " + buf.path.string(), true);
    }
}

void CodeEditor::saveAll() {
    int n = 0;
    std::string names;
    for (auto& buf : m_buffers) {
        if (buf.modified) {
            if (buf.save()) {
                ++n;
                if (!names.empty()) names += ", ";
                names += buf.path.filename().string();
            } else {
                m_editor->log("Save failed: " + buf.path.string(), true);
            }
        }
    }
    if (n == 0)      m_editor->log("Save All: nothing to save.");
    else if (n == 1) m_editor->log("Saved 1 file: " + names);
    else             m_editor->log("Saved " + std::to_string(n)
                                   + " files: " + names);
    if (n > 0) m_editor->notifySourceSaved();
}

void CodeEditor::closeAll() {
    m_buffers.clear();
    m_activeTab = -1;
}

// ─── Undo / redo / tab navigation / find ────────────────────────────────────

// Push pre-edit text onto the undo stack when a new edit burst starts.
// A "burst" is a run of edits separated by less than kSnapshotDebounceSec;
// passing the debounce threshold opens a new entry.
void CodeEditor::captureSnapshot(FileBuffer& buf, std::string preEdit) {
    const double now = ImGui::GetTime();

    // If this edit arrived within the debounce window, extend the current
    // burst — no new snapshot. We still advance the timestamp.
    const bool withinBurst =
        buf.lastSnapshotTime != 0.0
        && (now - buf.lastSnapshotTime) < kSnapshotDebounceSec
        && !buf.undoStack.empty();

    if (!withinBurst) {
        if (buf.undoStack.empty() || buf.undoStack.back() != preEdit) {
            buf.undoStack.push_back(std::move(preEdit));
            if (buf.undoStack.size() > kMaxUndo)
                buf.undoStack.erase(buf.undoStack.begin());
            buf.redoStack.clear();
        }
    }
    buf.lastSnapshotTime = now;
}

void CodeEditor::undo() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_buffers.size()) return;
    auto& buf = m_buffers[m_activeTab];
    if (buf.undoStack.empty()) return;

    buf.redoStack.push_back(buf.text);
    buf.text = std::move(buf.undoStack.back());
    buf.undoStack.pop_back();
    buf.modified = true;
    buf.lastSnapshotTime = ImGui::GetTime();
    if (buf.find.active) rebuildFindMatches(buf);
}

void CodeEditor::redo() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_buffers.size()) return;
    auto& buf = m_buffers[m_activeTab];
    if (buf.redoStack.empty()) return;

    buf.undoStack.push_back(buf.text);
    if (buf.undoStack.size() > kMaxUndo)
        buf.undoStack.erase(buf.undoStack.begin());
    buf.text = std::move(buf.redoStack.back());
    buf.redoStack.pop_back();
    buf.modified = true;
    buf.lastSnapshotTime = ImGui::GetTime();
    if (buf.find.active) rebuildFindMatches(buf);
}

void CodeEditor::nextTab() {
    if (m_buffers.empty()) return;
    int n = (int)m_buffers.size();
    m_pendingFocus = (m_activeTab + 1) % n;
}

void CodeEditor::prevTab() {
    if (m_buffers.empty()) return;
    int n = (int)m_buffers.size();
    m_pendingFocus = (m_activeTab - 1 + n) % n;
}

void CodeEditor::toggleFind() {
    if (m_activeTab < 0 || m_activeTab >= (int)m_buffers.size()) {
        m_visible = true;  // no buffer — at least ensure the panel is visible
        return;
    }
    auto& buf = m_buffers[m_activeTab];
    buf.find.active = !buf.find.active;
    if (buf.find.active) {
        buf.find.focusInput = true;
        rebuildFindMatches(buf);
    }
    m_visible = true;
}

void CodeEditor::rebuildFindMatches(FileBuffer& buf) {
    buf.find.matches.clear();
    buf.find.current = -1;
    if (buf.find.query[0] == '\0') return;

    const std::string q = buf.find.query;
    if (buf.find.caseSens) {
        size_t pos = 0;
        while ((pos = buf.text.find(q, pos)) != std::string::npos) {
            buf.find.matches.push_back(pos);
            pos += q.size();
        }
    } else {
        auto lower = [](unsigned char c) -> char {
            return (char)std::tolower(c);
        };
        std::string ql(q.size(), '\0');
        std::transform(q.begin(), q.end(), ql.begin(), lower);
        std::string tl(buf.text.size(), '\0');
        std::transform(buf.text.begin(), buf.text.end(), tl.begin(), lower);
        size_t pos = 0;
        while ((pos = tl.find(ql, pos)) != std::string::npos) {
            buf.find.matches.push_back(pos);
            pos += ql.size();
        }
    }
    if (!buf.find.matches.empty()) jumpToMatch(buf, 0);
}

void CodeEditor::jumpToMatch(FileBuffer& buf, int which) {
    if (buf.find.matches.empty()) return;
    int n = (int)buf.find.matches.size();
    which = ((which % n) + n) % n;   // wrap to [0, n)
    buf.find.current       = which;
    buf.find.pendingSelect = true;
    buf.find.pendingStart  = buf.find.matches[which];
    buf.find.pendingEnd    = buf.find.pendingStart + std::strlen(buf.find.query);
}

void CodeEditor::drawMenuItem() {
    ImGui::MenuItem("Code Editor", nullptr, &m_visible);
}

// ─── Draw ───────────────────────────────────────────────────────────────────

void CodeEditor::draw() {
    if (!m_visible) return;
    ImGui::Begin("Code Editor", &m_visible);

    if (m_buffers.empty()) {
        ImGui::TextDisabled("No file open — click a .lua file in the file browser.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##code_tabs",
                           ImGuiTabBarFlags_Reorderable |
                           ImGuiTabBarFlags_AutoSelectNewTabs))
    {
        for (int i = 0; i < (int)m_buffers.size(); ) {
            FileBuffer& buf = m_buffers[i];
            std::string label = buf.path.filename().string();
            if (buf.modified) label += " *";
            label += "##tab" + std::to_string(i);

            bool open = true;
            ImGuiTabItemFlags flags = 0;
            if (m_pendingFocus == i) flags |= ImGuiTabItemFlags_SetSelected;
            if (buf.modified)        flags |= ImGuiTabItemFlags_UnsavedDocument;

            bool selected = ImGui::BeginTabItem(label.c_str(), &open, flags);
            if (m_pendingFocus == i) m_pendingFocus = -1;

            if (selected) {
                m_activeTab = i;
                drawEditor(buf);
                ImGui::EndTabItem();
            }

            if (!open) {
                // ImGui returned the X-click; defer actual close for confirmation
                if (buf.modified) {
                    m_wantClose  = true;
                    m_closeIndex = i;
                    // Keep the tab for now — closeConfirm will pop it.
                    ++i;
                } else {
                    m_buffers.erase(m_buffers.begin() + i);
                    if (m_activeTab >= (int)m_buffers.size())
                        m_activeTab = (int)m_buffers.size() - 1;
                }
            } else {
                ++i;
            }
        }
        ImGui::EndTabBar();
    }

    drawCloseConfirm();
    ImGui::End();
}

void CodeEditor::drawEditor(FileBuffer& buf) {
    // ── Top toolbar: mode toggle ────────────────────────────────────────────
    bool editMode = (buf.mode == ViewMode::Edit);
    if (ImGui::SmallButton(editMode ? "Edit" : "Preview (read-only)")) {
        buf.mode = editMode ? ViewMode::Preview : ViewMode::Edit;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(buf.find.active ? "Close Find" : "Find (Ctrl+F)")) {
        buf.find.active = !buf.find.active;
        if (buf.find.active) {
            buf.find.focusInput = true;
            rebuildFindMatches(buf);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  Ctrl+Z undo  /  Ctrl+Y redo  /  Ctrl+Tab next");

    ImGui::Separator();

    if (buf.find.active) drawFindBar(buf);

    // ── Main pane ───────────────────────────────────────────────────────────
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float statusH = ImGui::GetTextLineHeightWithSpacing() + 2.f;
    ImVec2 paneSize = { avail.x, std::max(1.f, avail.y - statusH - 4.f) };

    ImGui::BeginChild("##editor_pane", paneSize, false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (buf.mode == ViewMode::Edit) drawEditPane(buf);
    else                            drawPreviewPane(buf);

    ImGui::EndChild();

    // ── Status bar ──────────────────────────────────────────────────────────
    ImGui::Separator();
    size_t lines = static_cast<size_t>(
                   std::count(buf.text.begin(), buf.text.end(), '\n')) + 1;
    size_t bytes = buf.text.size();
    ImGui::TextDisabled(" %s   |   %zu lines   |   %zu bytes   |   Lua   |   %s",
                        buf.path.string().c_str(), lines, bytes,
                        buf.modified ? "modified" : "clean");
}

// ── Find bar (shared by edit + preview) ─────────────────────────────────────

void CodeEditor::drawFindBar(FileBuffer& buf) {
    ImGui::PushID("##findbar");
    if (buf.find.focusInput) {
        ImGui::SetKeyboardFocusHere();
        buf.find.focusInput = false;
    }
    ImGui::SetNextItemWidth(260);
    bool queryEnter = ImGui::InputTextWithHint(
        "##q", "find in this file...",
        buf.find.query, sizeof(buf.find.query),
        ImGuiInputTextFlags_EnterReturnsTrue);
    bool queryChanged = ImGui::IsItemEdited();
    if (queryChanged) rebuildFindMatches(buf);

    ImGui::SameLine();
    if (ImGui::Checkbox("Aa", &buf.find.caseSens))
        rebuildFindMatches(buf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Match case");

    ImGui::SameLine();
    const int n = (int)buf.find.matches.size();
    if (ImGui::SmallButton("Prev") && n > 0)
        jumpToMatch(buf, buf.find.current - 1);
    ImGui::SameLine();
    if ((ImGui::SmallButton("Next") || queryEnter) && n > 0)
        jumpToMatch(buf, buf.find.current < 0 ? 0 : buf.find.current + 1);

    ImGui::SameLine();
    if (n == 0)
        ImGui::TextColored(buf.find.query[0] ? ImVec4{1.f,0.55f,0.35f,1.f}
                                             : ImVec4{0.5f,0.5f,0.55f,1.f},
                           "%s", buf.find.query[0] ? "no matches" : "");
    else
        ImGui::TextDisabled("Match %d of %d", buf.find.current + 1, n);

    ImGui::SameLine();
    if (ImGui::SmallButton("Close"))
        buf.find.active = false;
    ImGui::PopID();

    ImGui::Separator();
}

// ── Edit pane: a single InputTextMultiline that grows via CallbackResize ──
//
// The widget uses three callbacks, all multiplexed through one function:
//   - Resize : grow the backing std::string when ImGui needs more capacity
//   - Always : a one-shot "set selection" after Find → jumps the cursor to
//              the hit and scrolls it into view (InputTextMultiline's own
//              scroll follows the cursor).

void CodeEditor::drawEditPane(FileBuffer& buf) {
    struct UserData {
        std::string* text;
        FindState*   find;     // may be null
    };

    struct CB {
        static int dispatch(ImGuiInputTextCallbackData* d) {
            auto* u = static_cast<UserData*>(d->UserData);
            if (d->EventFlag & ImGuiInputTextFlags_CallbackResize) {
                u->text->resize(static_cast<size_t>(d->BufSize));
                d->Buf = u->text->data();
            }
            if (d->EventFlag & ImGuiInputTextFlags_CallbackAlways) {
                // Apply a one-shot selection-move requested by Find.
                if (u->find && u->find->pendingSelect) {
                    int s = (int)u->find->pendingStart;
                    int e = (int)u->find->pendingEnd;
                    d->CursorPos      = e;
                    d->SelectionStart = s;
                    d->SelectionEnd   = e;
                    u->find->pendingSelect = false;
                }
            }
            return 0;
        }
    };

    if (buf.text.capacity() < buf.text.size() + 1)
        buf.text.reserve(buf.text.size() + 1024);

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput
                              | ImGuiInputTextFlags_CallbackResize
                              | ImGuiInputTextFlags_CallbackAlways;

    // Cache the pre-edit text (as the widget writes the new content directly
    // through our buf.text pointer, we need a copy to snapshot). Cheap for
    // typical game.lua sizes; would want a dirty-flag optimization for
    // multi-MB files.
    std::string preEdit = buf.text;

    UserData ud{ &buf.text, &buf.find };

    ImVec2 sz = ImGui::GetContentRegionAvail();
    bool changed = ImGui::InputTextMultiline("##code",
                                             buf.text.data(),
                                             buf.text.capacity() + 1,
                                             sz, flags,
                                             &CB::dispatch, &ud);
    if (changed) {
        buf.text.resize(std::strlen(buf.text.c_str()));
        if (buf.text != preEdit) {    // InputText sometimes re-fires without real changes
            captureSnapshot(buf, std::move(preEdit));
            buf.modified = true;
            if (buf.find.active) rebuildFindMatches(buf);
        }
    }
}

// ── Preview pane: read-only syntax-highlighted view ─────────────────────────

void CodeEditor::drawPreviewPane(FileBuffer& buf) {
    const ImU32 COL_DEFAULT  = IM_COL32(200, 200, 210, 255);
    const ImU32 COL_KEYWORD  = IM_COL32(100, 160, 255, 255);
    const ImU32 COL_BUILTIN  = IM_COL32( 80, 210, 180, 255);
    const ImU32 COL_STRING   = IM_COL32(220, 170, 110, 255);
    const ImU32 COL_NUMBER   = IM_COL32(170, 220, 120, 255);
    const ImU32 COL_COMMENT  = IM_COL32(110, 120, 130, 255);
    const ImU32 COL_FUNCTION = IM_COL32(230, 200, 120, 255);
    const ImU32 COL_LINENUM  = IM_COL32( 80,  80, 100, 255);
    const ImU32 COL_MATCH_BG = IM_COL32(80, 120, 40,  110);
    const ImU32 COL_CURMATCH = IM_COL32(230, 180, 40, 180);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float char_w = ImGui::CalcTextSize("M").x;   // monospace-ish
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const float scroll_y   = ImGui::GetScrollY();
    const float viewport_h = ImGui::GetWindowHeight();
    const int first_line   = std::max(0, (int)(scroll_y / line_h) - 2);
    const int last_line    =       (int)((scroll_y + viewport_h) / line_h) + 2;

    // If Find asked us to jump to a match, scroll the match into view and
    // clear the flag. (The same pendingSelect flag is also consumed by the
    // edit-pane callback; in preview we just scroll.)
    if (buf.find.pendingSelect) {
        // Count newlines before the match to derive a target line.
        size_t lineHit = 0;
        for (size_t i = 0; i < buf.find.pendingStart && i < buf.text.size(); ++i)
            if (buf.text[i] == '\n') ++lineHit;
        const float targetY = lineHit * line_h;
        const float centeredY = targetY - viewport_h * 0.3f;
        ImGui::SetScrollY(std::max(0.f, centeredY));
        buf.find.pendingSelect = false;
    }

    // Per-line match lookup: only iterate matches in this line during
    // rendering, so large files stay fast even with many hits. Build a
    // sparse map of byte-offset → "this line contains matches starting here".
    const std::string qs = buf.find.query;
    const size_t      qn = qs.size();

    const std::string& src = buf.text;
    int    line_no    = 0;
    size_t line_start = 0;
    size_t matchIdx   = 0;   // cursor into buf.find.matches

    while (line_start <= src.size()) {
        size_t line_end = src.find('\n', line_start);
        const bool last = (line_end == std::string::npos);
        if (last) line_end = src.size();

        if (line_no >= first_line && line_no <= last_line) {
            const std::string line = src.substr(line_start, line_end - line_start);
            const float y = origin.y + line_no * line_h;

            // Draw match highlight rects (background) before any text
            if (qn > 0 && !buf.find.matches.empty()) {
                // Fast-forward matchIdx to first match at-or-after line_start
                while (matchIdx < buf.find.matches.size() &&
                       buf.find.matches[matchIdx] < line_start)
                    ++matchIdx;
                size_t m = matchIdx;
                while (m < buf.find.matches.size() &&
                       buf.find.matches[m] < line_end)
                {
                    size_t col_in_line = buf.find.matches[m] - line_start;
                    float x0 = origin.x + char_w * 5.f + col_in_line * char_w;
                    float x1 = x0 + qn * char_w;
                    ImU32 col = ((int)m == buf.find.current) ? COL_CURMATCH
                                                             : COL_MATCH_BG;
                    dl->AddRectFilled({x0, y}, {x1, y + line_h}, col);
                    ++m;
                }
            }

            // Line number gutter
            char num_buf[12];
            std::snprintf(num_buf, sizeof(num_buf), "%4d", line_no + 1);
            dl->AddText({origin.x, y}, COL_LINENUM, num_buf);

            float x = origin.x + char_w * 5.f;

            tokenizeLine(line,
                [&](const std::string& tok, TokenType type) {
                    ImU32 col = COL_DEFAULT;
                    switch (type) {
                        case TokenType::Keyword:  col = COL_KEYWORD;  break;
                        case TokenType::Builtin:  col = COL_BUILTIN;  break;
                        case TokenType::String:   col = COL_STRING;   break;
                        case TokenType::Number:   col = COL_NUMBER;   break;
                        case TokenType::Comment:  col = COL_COMMENT;  break;
                        case TokenType::Function: col = COL_FUNCTION; break;
                        default: break;
                    }
                    dl->AddText({x, y}, col, tok.c_str(),
                                tok.c_str() + tok.size());
                    x += ImGui::CalcTextSize(tok.c_str(),
                                             tok.c_str() + tok.size()).x;
                });
        }

        ++line_no;
        if (last) break;
        line_start = line_end + 1;
    }

    // Reserve scrollable space for all lines
    ImGui::Dummy({0.f, line_no * line_h});
}

// ── Tokenizer (line-scoped; doesn't handle Lua's [[...]] strings yet) ──────

template <class F>
void CodeEditor::tokenizeLine(const std::string& line, F&& emit) {
    size_t i = 0;
    const size_t n = line.size();
    while (i < n) {
        // -- line comment
        if (i + 1 < n && line[i] == '-' && line[i + 1] == '-') {
            emit(line.substr(i), TokenType::Comment);
            return;
        }
        // String literal
        if (line[i] == '"' || line[i] == '\'') {
            const char q = line[i];
            size_t j = i + 1;
            while (j < n) {
                if (line[j] == '\\' && j + 1 < n) { j += 2; continue; }
                if (line[j] == q) { ++j; break; }
                ++j;
            }
            emit(line.substr(i, j - i), TokenType::String);
            i = j;
            continue;
        }
        // Number (int, float, hex)
        if (std::isdigit(static_cast<unsigned char>(line[i])) ||
            (line[i] == '.' && i + 1 < n &&
             std::isdigit(static_cast<unsigned char>(line[i + 1]))))
        {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) ||
                             line[j] == '.' || line[j] == 'x' || line[j] == 'X'))
                ++j;
            emit(line.substr(i, j - i), TokenType::Number);
            i = j;
            continue;
        }
        // Identifier / keyword
        if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
            size_t j = i;
            while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) ||
                             line[j] == '_'))
                ++j;
            std::string word = line.substr(i, j - i);
            TokenType t = TokenType::Default;
            if      (isKeyword(word, LUA_KEYWORDS))      t = TokenType::Keyword;
            else if (isKeyword(word, THERMO_BUILTINS))   t = TokenType::Builtin;
            else if (j < n && line[j] == '(')            t = TokenType::Function;
            emit(word, t);
            i = j;
            continue;
        }
        // Single-char fallback
        emit(std::string(1, line[i]), TokenType::Default);
        ++i;
    }
}

// ── Unsaved-changes confirmation modal ─────────────────────────────────────

void CodeEditor::drawCloseConfirm() {
    if (m_wantClose) {
        ImGui::OpenPopup("Unsaved Changes");
        m_wantClose = false;
    }
    if (m_closeIndex < 0 || m_closeIndex >= (int)m_buffers.size()) return;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        auto& buf = m_buffers[m_closeIndex];
        ImGui::Text("%s has unsaved changes.", buf.path.filename().string().c_str());
        ImGui::Spacing();

        auto closeIt = [&]{
            m_buffers.erase(m_buffers.begin() + m_closeIndex);
            if (m_activeTab >= (int)m_buffers.size())
                m_activeTab = (int)m_buffers.size() - 1;
            m_closeIndex = -1;
            ImGui::CloseCurrentPopup();
        };

        if (ImGui::Button("Save and close", {140, 0})) {
            if (buf.save()) {
                m_editor->log("Saved: " + buf.path.filename().string());
                m_editor->notifySourceSaved();
                closeIt();
            } else {
                m_editor->log("Save failed: " + buf.path.string(), true);
                // Leave the tab open
                m_closeIndex = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {120, 0})) {
            closeIt();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            m_closeIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
