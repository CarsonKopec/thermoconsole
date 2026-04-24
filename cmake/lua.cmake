# ─────────────────────────────────────────────────────────────────────────────
# Vendored Lua — built as a static library from sources in vendor/lua/src/.
#
# Lua upstream doesn't ship a CMakeLists, so we define the target here and
# include() this file from runtime/CMakeLists.txt. After inclusion:
#   - target `lua` is a STATIC library with Lua's C API
#   - LUA_INCLUDE_DIR, LUA_LIBRARIES, LUA_FOUND, LUA_VERSION_STRING are set
#     in the including scope, so downstream code that expects FindLua's
#     outputs keeps working unchanged.
#
# The lua.c / luac.c command-line programs are intentionally NOT built —
# we embed the library, we don't need the standalone interpreter.
# ─────────────────────────────────────────────────────────────────────────────

# The github.com/lua/lua mirror puts sources at the repo root; the classic
# lua.org tarballs put them under src/. Pick whichever layout is present.
set(_LUA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../vendor/lua")
if(EXISTS "${_LUA_ROOT}/src/lua.h")
    set(_LUA_SRC "${_LUA_ROOT}/src")
elseif(EXISTS "${_LUA_ROOT}/lua.h")
    set(_LUA_SRC "${_LUA_ROOT}")
endif()

if(NOT _LUA_SRC OR NOT EXISTS "${_LUA_SRC}/lua.h")
    message(FATAL_ERROR
        "Vendored Lua not found at ${_LUA_ROOT}\n"
        "Run:  python thermo.py setup")
endif()

set(_LUA_CORE
    ${_LUA_SRC}/lapi.c
    ${_LUA_SRC}/lcode.c
    ${_LUA_SRC}/lctype.c
    ${_LUA_SRC}/ldebug.c
    ${_LUA_SRC}/ldo.c
    ${_LUA_SRC}/ldump.c
    ${_LUA_SRC}/lfunc.c
    ${_LUA_SRC}/lgc.c
    ${_LUA_SRC}/llex.c
    ${_LUA_SRC}/lmem.c
    ${_LUA_SRC}/lobject.c
    ${_LUA_SRC}/lopcodes.c
    ${_LUA_SRC}/lparser.c
    ${_LUA_SRC}/lstate.c
    ${_LUA_SRC}/lstring.c
    ${_LUA_SRC}/ltable.c
    ${_LUA_SRC}/ltm.c
    ${_LUA_SRC}/lundump.c
    ${_LUA_SRC}/lvm.c
    ${_LUA_SRC}/lzio.c
)

set(_LUA_LIBS
    ${_LUA_SRC}/lauxlib.c
    ${_LUA_SRC}/lbaselib.c
    ${_LUA_SRC}/lcorolib.c
    ${_LUA_SRC}/ldblib.c
    ${_LUA_SRC}/liolib.c
    ${_LUA_SRC}/lmathlib.c
    ${_LUA_SRC}/loadlib.c
    ${_LUA_SRC}/loslib.c
    ${_LUA_SRC}/lstrlib.c
    ${_LUA_SRC}/ltablib.c
    ${_LUA_SRC}/lutf8lib.c
    ${_LUA_SRC}/linit.c
)

add_library(lua STATIC ${_LUA_CORE} ${_LUA_LIBS})

target_include_directories(lua PUBLIC ${_LUA_SRC})

# Enable the platform-appropriate dynamic-loading path (for require('somelib')).
# We still build statically, but Lua's runtime dlopen path needs to match.
if(WIN32)
    # Lua defaults to LoadLibrary on Windows — nothing to define.
elseif(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
    target_link_libraries(lua PRIVATE m)
else()
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua PRIVATE m dl)
endif()

if(NOT MSVC)
    # Lua's sources trigger a handful of pedantic warnings — silence them
    # here rather than in the runtime's compile flags. Upstream code, not ours.
    target_compile_options(lua PRIVATE
        -Wno-deprecated-declarations
        -Wno-string-plus-int)
endif()

# Parse LUA_VERSION out of lua.h so downstream status messages match the
# installed version. Format in lua.h: #define LUA_VERSION_MAJOR "5"
file(READ "${_LUA_SRC}/lua.h" _LUA_H)
string(REGEX MATCH "LUA_VERSION_MAJOR[ \t]+\"([0-9]+)\"" _ "${_LUA_H}")
set(_LUA_VER_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "LUA_VERSION_MINOR[ \t]+\"([0-9]+)\"" _ "${_LUA_H}")
set(_LUA_VER_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "LUA_VERSION_RELEASE[ \t]+\"([0-9]+)\"" _ "${_LUA_H}")
set(_LUA_VER_RELEASE "${CMAKE_MATCH_1}")

# Expose FindLua-compatible variables to the caller. include() shares scope
# with the caller, so a plain set() (no PARENT_SCOPE) is what we want.
set(LUA_FOUND          TRUE)
set(LUA_INCLUDE_DIR    "${_LUA_SRC}")
set(LUA_LIBRARIES      lua)
set(LUA_VERSION_STRING "${_LUA_VER_MAJOR}.${_LUA_VER_MINOR}.${_LUA_VER_RELEASE}")
