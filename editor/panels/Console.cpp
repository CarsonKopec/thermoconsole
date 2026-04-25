/*
 * ThermoConsole Editor — Console panel implementation
 */

#include "Console.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <vector>

Console::Console(ThermoEditor* editor) : m_editor(editor) {}

void Console::addLine(const std::string& msg, bool isError) {
    Entry e;
    e.text    = msg;
    e.isError = isError;

    // Thread-safe timestamp (localtime is not reentrant on POSIX;
    // localtime_s on Windows, localtime_r on POSIX).
    std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char ts[12];
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d",
                  lt.tm_hour, lt.tm_min, lt.tm_sec);
    e.timestamp = ts;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.push_back(std::move(e));
    while (m_entries.size() > kMaxEntries) m_entries.pop_front();   // O(1)
    m_scrollToBottom = m_autoScroll;
}

void Console::draw() {
    if (!m_visible) return;
    ImGui::Begin("Console", &m_visible);

    // ── Toolbar ─────────────────────────────────────────────────────────────
    if (ImGui::SmallButton("Clear")) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Timestamps", &m_showTimestamps);
    ImGui::SameLine();
    ImGui::Checkbox("Errors only", &m_errorsOnly);
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine(0, 12);

    // Source filter — radio-ish segmented buttons. "All" shows everything,
    // "Game" shows only lines prefixed with [game] (emitted by GamePreview),
    // "Editor" shows everything else.
    ImGui::TextDisabled("Show:");
    ImGui::SameLine();
    auto srcBtn = [&](const char* label, Source s) {
        const bool sel = (m_source == s);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                                       {0.25f, 0.45f, 0.85f, 1.f});
        if (ImGui::SmallButton(label)) m_source = s;
        if (sel) ImGui::PopStyleColor();
        ImGui::SameLine(0, 2);
    };
    srcBtn("All",    Source::All);
    srcBtn("Editor", Source::EditorOnly);
    srcBtn("Game",   Source::GameOnly);

    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##filter", "filter...", m_filter, sizeof(m_filter));
    ImGui::Separator();

    // Snapshot under lock so rendering doesn't race with addLine()
    std::deque<Entry> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        snapshot = m_entries;
    }

    // Case-insensitive filter
    std::string filter = m_filter;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    auto matches = [&](const Entry& e) -> bool {
        if (m_errorsOnly && !e.isError) return false;
        const bool fromGame = (e.text.rfind("[game]", 0) == 0);
        if (m_source == Source::GameOnly   && !fromGame) return false;
        if (m_source == Source::EditorOnly &&  fromGame) return false;
        if (filter.empty()) return true;
        std::string lower(e.text.size(), '\0');
        std::transform(e.text.begin(), e.text.end(), lower.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return lower.find(filter) != std::string::npos;
    };

    // Build the visible index list (cheap — just ints)
    std::vector<int> visible;
    visible.reserve(snapshot.size());
    for (int i = 0; i < (int)snapshot.size(); ++i)
        if (matches(snapshot[i])) visible.push_back(i);

    // Log area
    ImGui::BeginChild("##console_log", {0, 0}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin((int)visible.size());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const Entry& e = snapshot[visible[row]];
            if (m_showTimestamps) {
                ImGui::TextDisabled("[%s]", e.timestamp.c_str());
                ImGui::SameLine();
            }
            if (e.isError) {
                ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%s", e.text.c_str());
            } else if (e.text.rfind("[game]", 0) == 0) {
                ImGui::TextColored({0.6f, 0.9f, 0.6f, 1.f}, "%s", e.text.c_str());
            } else {
                ImGui::TextUnformatted(e.text.c_str());
            }
        }
    }
    clipper.End();

    if (m_scrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
        m_scrollToBottom = false;
    }
    ImGui::EndChild();
    ImGui::End();
}

void Console::drawMenuItem() {
    ImGui::MenuItem("Console", nullptr, &m_visible);
}
