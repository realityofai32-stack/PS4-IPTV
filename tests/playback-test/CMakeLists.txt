# PS4 IPTV playback test (title IPTT00001).
#
# Built against exactly what upstream pPlay's PS4 target builds against:
#   - libcross2d from the pPlay checkout (external/pplay-reference/libcross2d, same commit)
#   - pPlay's find modules for mpv / FFmpeg / libass (external/pplay-reference/cmake)
#   - pPlay's PS4 link libraries (CMakeLists.txt PLATFORM_PS4 branch)
#   - pPlay's media_info.cpp / utility.cpp unmodified, and src/pplay/ copies of pPlay's player wrapper
# Configure with -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/ps4-windows.cmake (see scripts/build-playback-test.ps1).

cmake_minimum_required(VERSION 3.22)

project(playback_test C CXX)

if (NOT PS4)
    message(FATAL_ERROR "PS4 only: configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-windows.cmake")
endif ()

set(PLATFORM_PS4 ON CACHE BOOL "Sony PS4 target platform" FORCE)

set(APP_VERSION "0.2.0")
set(APP_TITLE_ID "IPTT00001")
set(APP_PKG_TITLE "PS4 IPTV Playback Test")
set(APP_PKG_VERSION "01.00")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PPLAY_DIR "${REPO_ROOT}/external/pplay-reference" CACHE PATH "pPlay checkout (scripts/build-pplay-reference.ps1)")
if (NOT EXISTS "${PPLAY_DIR}/libcross2d/CMakeLists.txt")
    message(FATAL_ERROR "pPlay checkout not found at ${PPLAY_DIR} - run scripts/build-pplay-reference.ps1 first")
endif ()

###################
# libcross2d (same sources/options pPlay uses)
###################
add_subdirectory("${PPLAY_DIR}/libcross2d" "${CMAKE_BINARY_DIR}/libcross2d")

###################
# pPlay dependencies, found the same way pPlay finds them
###################
list(APPEND CMAKE_MODULE_PATH "${PPLAY_DIR}/cmake")
find_package(BZip2 REQUIRED)
find_package(MPV REQUIRED)
find_package(ffmpeg COMPONENTS avfilter avcodec avformat avutil swscale swresample)
find_package(ass REQUIRED)

###################
# build info
###################
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M UTC" UTC)
execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${REPO_ROOT}" OUTPUT_VARIABLE BUILD_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${REPO_ROOT}" OUTPUT_VARIABLE BUILD_GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT BUILD_GIT_HASH)
    set(BUILD_GIT_HASH "unknown")
endif ()
if (BUILD_GIT_DIRTY)
    string(APPEND BUILD_GIT_HASH "-dirty")
endif ()
configure_file(src/build_info.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.h" @ONLY)

###################
# executable
###################
set(APP_SOURCES
        src/main.cpp
        src/app.cpp
        src/app_playback.cpp
        src/diag_log.cpp
        src/redact.cpp
        src/stream_config.cpp
        src/test_env.cpp
        src/ffmpeg_info.cpp
        src/net_check.cpp
        )
set(PPLAY_DERIVED_SOURCES
        src/pplay/mpv.cpp
        src/pplay/video_texture.cpp
        )
set(PPLAY_UNMODIFIED_SOURCES
        "${PPLAY_DIR}/src/media_info.cpp"
        "${PPLAY_DIR}/src/utility.cpp"
        )

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${PPLAY_DERIVED_SOURCES} ${PPLAY_UNMODIFIED_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
        src
        src/pplay
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${PPLAY_DIR}/src"
        ${MPV_INCLUDE_DIRS}
        ${ASS_INCLUDE_DIRS}
        ${FFMPEG_INCLUDE_DIRS}
        ${BZIP2_INCLUDE_DIR}
        )
# same as pPlay
target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
# stricter warnings for code written for this test (upstream pPlay files keep pPlay's flags)
set_source_files_properties(${APP_SOURCES} ${PPLAY_DERIVED_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wextra;-Wno-unused-parameter")

# pPlay PS4 link line: cross2d pscrap ${MPV_LIBRARY} ${ASS_LIBRARIES} ${FFMPEG_LIBRARIES} ${BZIP2_LIBRARIES}
#                      opus freetype fribidi png bz2   (pscrap adds curl json-c mbedtls ... SceNet)
# The scraper (pscrap) is not part of playback; SceNet is kept so the same system modules are loaded.
target_link_libraries(${PROJECT_NAME}
        cross2d
        ${MPV_LIBRARY}
        ${ASS_LIBRARIES}
        ${FFMPEG_LIBRARIES}
        ${BZIP2_LIBRARIES}
        opus freetype fribidi png bz2
        SceNet
        )

###################
# PKG
###################
# Private stream configuration (git-ignored, contains credentials) packaged at the PKG root, which the
# app reads as /app0/test_streams.txt - the romfs mechanism pPlay uses for its skin (libcross2d
# PS4Io::getRomFsPath() = "/app0/"). The resulting PKG must therefore never be shared.
set(PS4IPTV_TEST_STREAMS_FILE "${REPO_ROOT}/config/test_streams.txt" CACHE FILEPATH
        "private TS/HLS URL file packaged into the playback test PKG")

set(PKG_ROMFS "${CMAKE_BINARY_DIR}/pkg_romfs")
add_custom_target(${PROJECT_NAME}_data
        COMMAND ${CMAKE_COMMAND} -DSTREAMS_FILE=${PS4IPTV_TEST_STREAMS_FILE}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_test_streams.cmake"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${PKG_ROMFS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_ROMFS}/sce_sys"
        # same system modules / about file as pPlay's PKG (data/ps4/romfs)
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PPLAY_DIR}/data/ps4/romfs/sce_module" "${PKG_ROMFS}/sce_module"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PPLAY_DIR}/data/ps4/romfs/sce_sys/about" "${PKG_ROMFS}/sce_sys/about"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png" "${PKG_ROMFS}/sce_sys/icon0.png"
        COMMAND ${CMAKE_COMMAND} -E copy "${PS4IPTV_TEST_STREAMS_FILE}" "${PKG_ROMFS}/test_streams.txt"
        VERBATIM
        )

add_self(${PROJECT_NAME})
add_pkg(${PROJECT_NAME} "${PKG_ROMFS}" ${APP_TITLE_ID} "${APP_PKG_TITLE}" ${APP_PKG_VERSION})
add_dependencies(${PROJECT_NAME}_pkg ${PROJECT_NAME}_data)
