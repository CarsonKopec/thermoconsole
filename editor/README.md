# ThermoConsole Editor

A Dear ImGui + SDL2 game development editor for the ThermoConsole handheld.
Edit Lua source, draw sprites on a 16-colour PICO-8-style palette, tweak the
manifest, and launch the runtime — all from one window.

## Build

```
./setup.sh                     # clones ImGui, configures, builds
./build/thermo_editor <folder> # open a game folder directly
```

Dependencies:

- SDL2 + SDL2_image  (`apt install libsdl2-dev libsdl2-image-dev`, `brew install sdl2 sdl2_image`, or vcpkg)
- CMake ≥ 3.16
- A C++17 compiler

The build defaults to `Release`. For a debug build:

```
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

## Panels

| Panel         | What it does                                                           |
|---------------|------------------------------------------------------------------------|
| Files         | Recursive collapsible tree of the project. Right-click for ops.        |
| Code Editor   | Multi-tab editor. Toggle per tab between raw edit and highlighted preview. |
| Sprite Editor | 256×256 sheet with 16-colour palette, undo/redo, flood fill.           |
| Manifest      | Edit `manifest.json` fields with type-safe inputs.                     |
| Game Preview  | Launch the runtime; stdout/stderr stream to the console.               |
| Console       | Filterable, timestamped log view. Auto-scroll toggle.                  |

## Keyboard Shortcuts

| Action                       | Shortcut              |
|------------------------------|-----------------------|
| Open project                 | `Ctrl+O`              |
| Save current file            | `Ctrl+S`              |
| Save all files               | `Ctrl+Shift+S`        |
| Quit                         | `Ctrl+Q`              |
| Run game                     | `F5`                  |
| Stop game                    | `F6`                  |
| Undo (sprite editor)         | `Ctrl+Z`              |
| Redo (sprite editor)         | `Ctrl+Shift+Z` / `Ctrl+Y` |

Shortcuts no longer get eaten by whichever window happens to be focused —
`Ctrl+S` works from inside the code editor, and `F5`/`F6` fire anywhere.

## What changed in 1.0.1

This release is a quality pass on 1.0.0. Real bugs, not cosmetic:

### GamePreview — process lifecycle rewritten
- The reader thread is now joined on shutdown instead of detached; the old
  code could UAF when the panel (or whole editor) was destroyed mid-read.
- All process/pipe/pid access is mutex-protected. Previously the reader thread
  and UI thread raced on the same handles — a crash waiting to happen on exit.
- POSIX reads use non-blocking I/O + `poll()` with a 50 ms timeout so stop
  requests don't wait for the next line of output.
- Windows reads use `PeekNamedPipe` before `ReadFile` for the same reason.
- Kill escalates `SIGTERM` → wait → `SIGKILL` on POSIX instead of one-shot.
- **ROM packing is now async.** The old `system()` call blocked the UI; now
  it runs on a `std::async` thread and the button disables while it's working.

### CodeEditor — single-widget design
- The previous "highlighted overlay behind an `InputTextMultiline`" had ghost
  text and ate edits in the wrong location. That approach has been replaced
  by a clean single widget plus a per-tab **Edit / Preview toggle**.
  - *Edit*: plain `InputTextMultiline` that actually works.
  - *Preview*: read-only syntax-highlighted view.
- The resize callback now uses `BufSize` (not `BufTextLen`), so the buffer
  has room for its null terminator.
- Closing a modified tab now shows a **Save / Discard / Cancel** dialog
  instead of silently discarding your work.
- Modified tabs show an unsaved-document indicator (`*` and ImGui's built-in
  unsaved-document dot).

### FileBrowser — real recursive tree
- Directories are now proper `TreeNode`s that collapse and expand. Before,
  *every* node was marked as a leaf, so directories could never be collapsed.
- Delete and rename no longer invalidate the iterator mid-loop — operations
  are queued and applied after the tree walk.
- Delete shows a confirmation dialog. Folders are removed with `remove_all`
  (the old code silently failed on any non-empty folder).
- Rename is implemented.
- Filtering force-expands matching directories so results are visible.

### SpriteEditor — undo + transparency consistency
- **Undo / redo** (64-step ring, whole-sheet snapshots). One click-drag
  counts as one undo step.
- Palette index 0 is now consistently transparent: saved PNGs have `α=0` at
  those pixels, and loading a PNG treats `α<128` as index 0. Previously the
  canvas showed a checkerboard but the saved image was opaque black.
- Grid size is restricted to divisors of 256 (`8 / 16 / 32 / 64`) so tiles
  always align; no more orphan columns/rows at the edge.
- Palette lookups are bounds-masked — the old tooltip read out of the
  palette array if the pixel was ever out of range.

### Console — thread-safe + O(1) trim
- `std::deque` with `pop_front()` instead of `vector::erase(begin)` when
  trimming. The old O(n) trim would drop the editor to a crawl after a few
  thousand log lines.
- Timestamping now uses `localtime_r` (POSIX) / `localtime_s` (Windows)
  instead of the non-reentrant `localtime()`.
- A mutex snapshots the entries before rendering, so the reader thread's
  pushes no longer race with the UI draw.
- Filter is case-insensitive and an auto-scroll toggle was added.

### ThermoEditor — shortcuts that actually fire
- The old handler early-returned on `io.WantCaptureKeyboard`, which is true
  whenever any ImGui window has focus. Result: none of the shortcuts ever
  worked. The new handler uses `io.WantTextInput` (true only while the user
  is actively editing a text field) and still lets `Ctrl+S` / `F5` / `F6`
  through during text input.
- `Ctrl+O` previously called `OpenPopup` with no matching `BeginPopupModal`
  anywhere — silently did nothing. The Open-project modal is now hosted on
  the main window so the menu item, the welcome screen, and the shortcut
  all share a single code path.
- The manifest JSON parser wraps `std::stoi` in try/catch (malformed numeric
  fields used to crash the editor) and clamps dimensions to sane ranges.
- Shutdown order is fixed: panels are destroyed before SDL, so
  `GamePreview` can join its reader thread while its textures are still
  valid.

### ManifestEditor
- Fields use a `snprintf`-based copy that always null-terminates (the old
  `strncpy(dst, src, N-1)` could leave a non-terminated buffer if the source
  was exactly `N-1` bytes long).
- Width / height / grid-size are clamped to valid ranges.
- Orientation is a dropdown, not free text.

## License

MIT. Vendored ImGui retains its MIT license too.
