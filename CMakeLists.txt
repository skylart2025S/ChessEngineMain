cmake_minimum_required(VERSION 3.16)
project(chess C)

# ───────────────────────────────────────────────
#  Compiler warnings (MSVC vs GCC/Clang)
# ───────────────────────────────────────────────

if(MSVC)
    set(WARN_FLAGS /W4)
else()
    set(WARN_FLAGS -Wall -Wextra)
endif()

# ───────────────────────────────────────────────
#  Sources
# ───────────────────────────────────────────────

set(ENGINE_SRC main.c)
set(GUI_SRC    display.c)

# ───────────────────────────────────────────────
#  Terminal build (always available)
# ───────────────────────────────────────────────

add_executable(chess_terminal ${ENGINE_SRC})
target_compile_options(chess_terminal PRIVATE ${WARN_FLAGS})
target_compile_definitions(chess_terminal PRIVATE TERMINAL_MODE=1)

# ───────────────────────────────────────────────
#  SDL3 GUI build (optional)
# ───────────────────────────────────────────────

option(USE_SDL "Build SDL3 GUI" ON)

if(USE_SDL)
    find_package(SDL3 QUIET CONFIG)

    if(SDL3_FOUND)
        message(STATUS "Found SDL3 via CMake config")
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL3 QUIET sdl3)
        endif()
    endif()

    if(SDL3_FOUND OR TARGET SDL3::SDL3)
        add_executable(chess_gui ${ENGINE_SRC} ${GUI_SRC})
        target_compile_options(chess_gui PRIVATE ${WARN_FLAGS})
        target_compile_definitions(chess_gui PRIVATE GUI_MODE=1)

        # Force console window so SDL errors are visible during development
        if(MINGW)
            target_link_options(chess_gui PRIVATE -mconsole)
        endif()

        if(TARGET SDL3::SDL3)
            target_link_libraries(chess_gui PRIVATE SDL3::SDL3)
        else()
            target_link_libraries(chess_gui PRIVATE ${SDL3_LIBRARIES})
            target_include_directories(chess_gui PRIVATE ${SDL3_INCLUDE_DIRS})
        endif()

        # Copy SDL3.dll next to the exe after build so it can be found at runtime
        if(MINGW)
            find_file(SDL3_DLL
                NAMES SDL3.dll
                PATHS C:/msys64/ucrt64/bin
                      C:/msys64/mingw64/bin
                NO_DEFAULT_PATH
            )
            if(SDL3_DLL)
                add_custom_command(TARGET chess_gui POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${SDL3_DLL}
                        $<TARGET_FILE_DIR:chess_gui>
                    COMMENT "Copying SDL3.dll to output directory"
                )
                message(STATUS "SDL3.dll will be copied from: ${SDL3_DLL}")
            else()
                message(WARNING "SDL3.dll not found — copy it manually next to chess_gui.exe")
            endif()
        endif()

        message(STATUS "chess_gui target created")
    else()
        message(WARNING "SDL3 not found → skipping chess_gui target")
    endif()
endif()