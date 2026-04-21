/*
 * ThermoConsole Editor
 * main.cpp — application entry point
 */

#include "ThermoEditor.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    ThermoEditor editor;

    if (!editor.init()) {
        fprintf(stderr, "Failed to initialise editor.\n");
        return 1;
    }

    // If a path was passed on the command line, open it immediately
    if (argc >= 2) {
        editor.openProject(argv[1]);
    }

    editor.run();
    editor.shutdown();
    return 0;
}
