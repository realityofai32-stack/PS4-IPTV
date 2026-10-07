# PS4 IPTV - production application (title IPTV00002).
#
# Playback foundation (Milestone B+): the pPlay stack proven on hardware in Phase 2 - libmpv 0.34.1,
# FFmpeg 5.0, SDL2 2.0.18 PS4 port, libcross2d - found and linked exactly like upstream pPlay.
# Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-windows.cmake (see scripts/build-ps4iptv.ps1).

cmake_minimum_required(VERSION 3.22)

project(ps4iptv C CXX)

if (NOT PS4)
    message(FATAL_ERROR "PS4 only: configure with -DCMAKE_TOOLCHAIN_FILE=cmake/ps4-windows.cmake")
endif ()

set(APP_VERSION "0.1.0")
set(APP_TITLE_ID "IPTV00002")      # unique: not PPLA00001 (pPlay), IPTV00001 (old experiment), IPTT00001 (test)
set(APP_PKG_TITLE "PS4 IPTV")
set(APP_PKG_VERSION "00.10")       # PS4 APP_VER format XX.YY for 0.1.0

set(PLATFORM_PS4 ON CACHE BOOL "Sony PS4 target platform" FORCE)
set(PPLAY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/pplay-reference" CACHE PATH "pPlay checkout")
if (NOT EXISTS "${PPLAY_DIR}/libcross2d/CMakeLists.txt")
    message(FATAL_ERROR "pPlay checkout not found at ${PPLAY_DIR} - run scripts/build-pplay-reference.ps1 first")
endif ()

add_subdirectory("${PPLAY_DIR}/libcross2d" "${CMAKE_BINARY_DIR}/libcross2d")

list(APPEND CMAKE_MODULE_PATH "${PPLAY_DIR}/cmake")
find_package(BZip2 REQUIRED)
find_package(MPV REQUIRED)
find_package(ffmpeg COMPONENTS avfilter avcodec avformat avutil swscale swresample)
find_package(ass REQUIRED)

# build info
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M UTC" UTC)
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE BUILD_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE BUILD_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT BUILD_GIT_HASH)
    set(BUILD_GIT_HASH "unknown")
endif ()
if (BUILD_GIT_DIRTY)
    string(APPEND BUILD_GIT_HASH "-dirty")
endif ()
configure_file(src/build_info.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.h" @ONLY)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp)
# unmodified pPlay sources needed by the pPlay Mpv wrapper (src/player/pplay)
set(PPLAY_UNMODIFIED_SOURCES "${PPLAY_DIR}/src/media_info.cpp" "${PPLAY_DIR}/src/utility.cpp")

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${PPLAY_UNMODIFIED_SOURCES})
set_source_files_properties(${PPLAY_UNMODIFIED_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-extra")
target_include_directories(${PROJECT_NAME} PRIVATE
        src
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
        "${PPLAY_DIR}/src"
        ${MPV_INCLUDE_DIRS} ${ASS_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIR})
target_compile_options(${PROJECT_NAME} PRIVATE -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# hardware text-rendering checkpoint: open the text test screen before the normal first screen
option(PS4IPTV_TEXT_TEST_AT_START "Show the text rendering test screen at launch" ON)
if (PS4IPTV_TEXT_TEST_AT_START)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PS4IPTV_TEXT_TEST_AT_START=1)
endif ()
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE PS4IPTV_DEBUG=1)
endif ()

# pPlay PS4 link libraries (playback stack) + libcurl/mbedTLS for the Xtream API
target_link_libraries(${PROJECT_NAME}
        cross2d
        ${MPV_LIBRARY} ${ASS_LIBRARIES} ${FFMPEG_LIBRARIES} ${BZIP2_LIBRARIES}
        opus freetype fribidi png bz2
        curl mbedtls mbedx509 mbedcrypto z
        SceNet)

###################
# PKG
###################
set(PKG_ROMFS "${CMAKE_BINARY_DIR}/pkg_romfs")
add_custom_target(${PROJECT_NAME}_data
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${PKG_ROMFS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG_ROMFS}/sce_sys" "${PKG_ROMFS}/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PPLAY_DIR}/data/ps4/romfs/sce_module" "${PKG_ROMFS}/sce_module"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PPLAY_DIR}/data/ps4/romfs/sce_sys/about" "${PKG_ROMFS}/sce_sys/about"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon0.png" "${PKG_ROMFS}/sce_sys/icon0.png"
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/Inter-Regular.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/Inter-SemiBold.ttf"
                "${PKG_ROMFS}/assets/fonts/"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cacert/cacert.pem" "${PKG_ROMFS}/assets/cacert.pem"
        VERBATIM)

add_self(${PROJECT_NAME})

# credential guard: runs after the executable and the romfs exist, before the PKG is built
add_custom_target(${PROJECT_NAME}_credential_guard
        COMMAND ${CMAKE_COMMAND}
                "-DSCAN_DIR=${PKG_ROMFS}"
                "-DSCAN_FILES=${CMAKE_BINARY_DIR}/eboot.bin|${CMAKE_BINARY_DIR}/${PROJECT_NAME}"
                "-DSECRET_SOURCES=${CMAKE_CURRENT_SOURCE_DIR}/config/test_streams.txt|${CMAKE_CURRENT_SOURCE_DIR}/config/forbidden_strings.txt"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/scan_credentials.cmake"
        VERBATIM)
add_dependencies(${PROJECT_NAME}_credential_guard ${PROJECT_NAME}_data ${PROJECT_NAME}_self)

add_pkg(${PROJECT_NAME} "${PKG_ROMFS}" ${APP_TITLE_ID} "${APP_PKG_TITLE}" ${APP_PKG_VERSION})
add_dependencies(${PROJECT_NAME}_pkg ${PROJECT_NAME}_credential_guard)
