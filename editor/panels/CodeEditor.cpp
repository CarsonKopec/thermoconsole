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
    if (buf.save())
        m_editor->log("Saved: " + buf.path.filename().string());
    else
        m_editor->log("Save failed: " + buf.path.string(), true);
}

void CodeEditor::saveAll() {
    int n = 0;
    for (auto& buf : m_buffers) {
        if (buf.modified) {
            if (buf.save()) ++n;
            else m_editor->log("Save failed: " + buf.path.string(), true);
        }
    }
    m_editor->log("Saved " + std::to_string(n) + " file"
                  + (n == 1 ? "" : "s") + ".");
}

void CodeEditor::closeAll() {
    m_buffers.clear();
    m_activeTab = -1;
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
    ImGui::TextDisabled("  Click to toggle between edit and syntax-highlighted preview");

    ImGui::Separator();

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

// ── Edit pane: a single InputTextMultiline that grows via CallbackResize ──

void CodeEditor::drawEditPane(FileBuffer& buf) {
    // Callback data struct so we can capture buf by pointer
    struct CB {
        static int resize(ImGuiInputTextCallbackData* d) {
            if (d->EventFlag & ImGuiInputTextFlags_CallbackResize) {
                auto* s = static_cast<std::string*>(d->UserData);
                // ImGui requires the buffer be at least BufSize bytes
                s->resize(static_cast<size_t>(d->BufSize));
                d->Buf = s->data();
            }
            return 0;
        }
    };

    // Make sure the string has at least 1 byte of storage for the null
    // terminator — std::string::data() returns a null-terminated pointer in
    // C++17+, but we want spare capacity so the widget doesn't immediately
    // trigger a resize on first character.
    if (buf.text.capacity() < buf.text.size() + 1)
        buf.text.reserve(buf.text.size() + 1024);

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput
                              | ImGuiInputTextFlags_CallbackResize;

    ImVec2 sz = ImGui::GetContentRegionAvail();
    if (ImGui::InputTextMultiline("##code",
                                  buf.text.data(),
                                  buf.text.capacity() + 1,
                                  sz, flags,
                                  &CB::resize, &buf.text))
    {
        // InputText writes the current length via strlen() — trim the string
        // so m_text.size() matches the actual content.
        buf.text.resize(std::strlen(buf.text.c_str()));
        buf.modified = true;
    }
}

// ── Preview pane: read-only syntax-highlighted view ─────────────────────────

void CodeEditor::drawPreviewPane(const FileBuffer& buf) {
    const ImU32 COL_DEFAULT  = IM_COL32(200, 200, 210, 255);
    const ImU32 COL_KEYWORD  = IM_COL32(100, 160, 255, 255);
    const ImU32 COL_BUILTIN  = IM_COL32( 80, 210, 180, 255);
    const ImU32 COL_STRING   = IM_COL32(220, 170, 110, 255);
    const ImU32 COL_NUMBER   = IM_COL32(170, 220, 120, 255);
    const ImU32 COL_COMMENT  = IM_COL32(110, 120, 130, 255);
    const ImU32 COL_FUNCTION = IM_COL32(230, 200, 120, 255);
    const ImU32 COL_LINENUM  = IM_COL32( 80,  80, 100, 255);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float char_w = ImGui::CalcTextSize("M").x;   // monospace-ish
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const float scroll_y   = ImGui::GetScrollY();
    const float viewport_h = ImGui::GetWindowHeight();
    const int first_line   = std::max(0, (int)(scroll_y / line_h) - 2);
    const int last_line    =       (int)((scroll_y + viewport_h) / line_h) + 2;

    const std::string& src = buf.text;
    int    line_no    = 0;
    size_t line_start = 0;

    while (line_start <= src.size()) {
        size_t line_end = src.find('\n', line_start);
        const bool last = (line_end == std::string::npos);
        if (last) line_end = src.size();

        if (line_no >= first_line && line_no <= last_line) {
            const std::string line = src.substr(line_start, line_end - line_start);
            const float y = origin.y + line_no * line_h;

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
