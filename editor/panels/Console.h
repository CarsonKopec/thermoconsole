#pragma once

/*
 * ThermoConsole Editor
 * Console panel — scrolling, filterable, colored log output.
 *
 * addLine() is thread-safe so GamePreview's reader thread can funnel game
 * stdout directly without staging through the UI thread.
 */

#include "../ThermoEditor.h"

#include <deque>
#include <mutex>
#include <string>

class Console {
public:
    explicit Console(ThermoEditor* editor);

    void addLine(const std::string& msg, bool isError = false);
    void draw();
    void drawMenuItem();

private:
    struct Entry {
        std::string text;
        std::string timestamp;
        bool        isError = false;
    };

    ThermoEditor*       m_editor;
    mutable std::mutex  m_mutex;
    std::deque<Entry>   m_entries;
    bool                m_visible        = true;
    bool                m_scrollToBottom = false;
    bool                m_showTimestamps = true;
    bool                m_errorsOnly     = false;
    bool                m_autoScroll     = true;
    char                m_filter[128] {};

    static constexpr size_t kMaxEntries = 2000;
};
