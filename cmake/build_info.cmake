# Writes generated/build_info.h at every build (not only when CMake configures): the git commit and its "-dirty"
# mark always describe the sources that were compiled. configure_file only rewrites the header when its content
# changes (the build time has minute resolution: a rebuild within the same minute recompiles nothing).
# Inputs: -DSRC_DIR -DOUT_FILE -DAPP_VERSION -DAPP_TITLE_ID -DBUILD_TYPE
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE BUILD_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE BUILD_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT BUILD_GIT_HASH)
    set(BUILD_GIT_HASH "unknown")
endif ()
if (BUILD_GIT_DIRTY)
    string(APPEND BUILD_GIT_HASH "-dirty")
endif ()
string(TIMESTAMP BUILD_DATE "%Y-%m-%d %H:%M UTC" UTC)
set(CMAKE_BUILD_TYPE "${BUILD_TYPE}")
configure_file("${SRC_DIR}/src/build_info.h.in" "${OUT_FILE}" @ONLY)
