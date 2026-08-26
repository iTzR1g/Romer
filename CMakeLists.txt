cmake_minimum_required(VERSION 3.16)
project(romer VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(JPEG REQUIRED)

# ─── Platform ─────────────────────────────────────────────────
if(WIN32)
    set(PLATFORM_CAPTURE src/capture_win.cpp)
    set(PLATFORM_LIBS ws2_32 d3d11 dxgi)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0601)
elseif(APPLE)
    set(PLATFORM_CAPTURE src/capture_mac.mm)
    find_library(CORE_GRAPHICS   CoreGraphics   REQUIRED)
    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    set(PLATFORM_LIBS ${CORE_GRAPHICS} ${CORE_FOUNDATION})
    set(CMAKE_CXX_EXTENSIONS OFF)
else()
    set(PLATFORM_CAPTURE src/capture_linux.cpp)
    find_package(X11 REQUIRED)
    list(APPEND PLATFORM_LIBS ${X11_LIBRARIES})
    if(X11_Xext_FOUND)
        list(APPEND PLATFORM_LIBS ${X11_Xext_LIBRARY})
    endif()
    find_library(UTIL_LIB util)
    if(UTIL_LIB)
        list(APPEND PLATFORM_LIBS ${UTIL_LIB})
    endif()
endif()

# ─── Detect optional UI / viewer deps ──────────────────────
find_package(SDL2     QUIET)
find_package(SDL2_ttf QUIET)
find_package(PkgConfig QUIET)

if(NOT SDL2_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SDL2_PKG SDL2)
    if(SDL2_PKG_FOUND)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS ${SDL2_PKG_INCLUDE_DIRS})
        set(SDL2_LIBRARIES ${SDL2_PKG_LIBRARIES})
        if(NOT TARGET SDL2::SDL2)
            add_library(SDL2::SDL2 INTERFACE IMPORTED)
            target_include_directories(SDL2::SDL2 INTERFACE ${SDL2_INCLUDE_DIRS})
            target_link_libraries(SDL2::SDL2 INTERFACE ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

if(NOT SDL2_ttf_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SDL2_TTF_PKG SDL2_ttf)
    if(SDL2_TTF_PKG_FOUND)
        set(SDL2_ttf_FOUND TRUE)
        set(SDL2_ttf_INCLUDE_DIRS ${SDL2_TTF_PKG_INCLUDE_DIRS})
        set(SDL2_ttf_LIBRARIES ${SDL2_TTF_PKG_LIBRARIES})
        if(NOT TARGET SDL2_ttf::SDL2_ttf)
            add_library(SDL2_ttf::SDL2_ttf INTERFACE IMPORTED)
            target_include_directories(SDL2_ttf::SDL2_ttf INTERFACE ${SDL2_ttf_INCLUDE_DIRS})
            target_link_libraries(SDL2_ttf::SDL2_ttf INTERFACE ${SDL2_ttf_LIBRARIES})
        endif()
    endif()
endif()

if(SDL2_FOUND AND SDL2_ttf_FOUND)
    set(HAS_UI        TRUE)
    set(UI_SOURCE     src/ui.cpp)
    set(UI_DEFS       HAS_TTF=1)
    set(UI_LIBS       SDL2::SDL2 SDL2_ttf::SDL2_ttf)
    message(STATUS "SDL2 + SDL2_ttf found – full graphical UI enabled")

elseif(SDL2_FOUND)
    set(HAS_UI        FALSE)
    set(UI_SOURCE     "")
    set(UI_DEFS       "")
    set(UI_LIBS       SDL2::SDL2)
    message(WARNING "SDL2_ttf not found – UI mode disabled. "
                    "CLI modes (--server --send --recv --shell <host>) still work. "
                    "Install libsdl2-ttf-dev (Linux) or sdl2_ttf (Homebrew/vcpkg).")
else()
    set(HAS_UI        FALSE)
    set(UI_SOURCE     "")
    set(UI_DEFS       NO_SDL=1)
    set(UI_LIBS       "")
    message(WARNING "SDL2 not found – viewer and UI disabled. "
                    "CLI modes (--server --send --recv --shell) still work. "
                    "Install libsdl2-dev + libsdl2-ttf-dev (Linux) or "
                    "SDL2 + SDL2_ttf via Homebrew/vcpkg.")
endif()

# ─── Core sources (always compiled) ────────────────────────
set(CORE_SOURCES
    src/main.cpp
    src/jpeg_utils.cpp
    src/stream.cpp
    src/file_transfer.cpp
    src/remote_shell.cpp
    ${PLATFORM_CAPTURE}
)

# ─── The one binary ─────────────────────────────────────────
add_executable(romer ${CORE_SOURCES} ${UI_SOURCE})
target_include_directories(romer PRIVATE src)
target_compile_definitions(romer PRIVATE ${UI_DEFS})
target_link_libraries(romer PRIVATE JPEG::JPEG ${UI_LIBS} ${PLATFORM_LIBS})
