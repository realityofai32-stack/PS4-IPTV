cmake_minimum_required(VERSION 3.22)

# Production package guard: fails if private test credentials or config files would be packaged.
#
#   cmake -DSCAN_DIR=<pkg romfs> -DSCAN_FILES=<eboot;elf> -DSECRET_SOURCES=<file1;file2> -P scan_credentials.cmake
#
# - SCAN_DIR must not contain test_streams.txt, *.local.*, profiles.json, settings.json, favorites/history files
# - no file in SCAN_DIR and none of SCAN_FILES may contain a secret: usernames/passwords and full URLs taken
#   from SECRET_SOURCES (config/test_streams.txt: Xtream /live/<user>/<pass>/ URLs; config/forbidden_strings.txt:
#   one literal per line). Secret values are never printed.

# lists are passed with '|' separators (a ';' does not survive the Ninja/cmd command line)
string(REPLACE "|" ";" SCAN_FILES "${SCAN_FILES}")
string(REPLACE "|" ";" SECRET_SOURCES "${SECRET_SOURCES}")
# accept Windows paths with backslashes (EXISTS/GLOB need CMake-style paths)
file(TO_CMAKE_PATH "${SCAN_DIR}" SCAN_DIR)
file(TO_CMAKE_PATH "${SCAN_FILES}" SCAN_FILES)
file(TO_CMAKE_PATH "${SECRET_SOURCES}" SECRET_SOURCES)

set(forbidden_names "test_streams.txt;test_stream.txt;profiles.json;profiles.v1.json;settings.json;favorites.json;history.json;progress.json;m3u_playlist.json;m3u_playlist.meta")

file(GLOB_RECURSE packaged LIST_DIRECTORIES false "${SCAN_DIR}/*")
foreach (f IN LISTS packaged)
    get_filename_component(name "${f}" NAME)
    string(TOLOWER "${name}" lname)
    # M3U / M3U8 playlists (private test lists, saved copies) carry stream URLs with credentials or tokens
    if (lname IN_LIST forbidden_names OR lname MATCHES "\\.local\\." OR lname MATCHES "\\.m3u8?$")
        message(FATAL_ERROR "CREDENTIAL GUARD: ${f} must never be packaged in the production PKG")
    endif ()
endforeach ()

# collect secrets
set(secrets "")
set(sources_with_content 0)
foreach (src IN LISTS SECRET_SOURCES)
    if (NOT EXISTS "${src}")
        continue()
    endif ()
    file(STRINGS "${src}" lines)
    math(EXPR sources_with_content "${sources_with_content} + 1")
    foreach (line IN LISTS lines)
        string(STRIP "${line}" line)
        if (line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif ()
        if (src MATCHES "forbidden_strings")
            list(APPEND secrets "${line}")
            continue()
        endif ()
        if (line MATCHES "^[A-Za-z_]+[ \t]*=[ \t]*(.+)$")
            set(value "${CMAKE_MATCH_1}")
            if (value MATCHES "<")
                continue()
            endif ()
            list(APPEND secrets "${value}")
            if (value MATCHES "/(live|movie|series|timeshift)/([^/]+)/([^/]+)/")
                list(APPEND secrets "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}")
            endif ()
            foreach (key username password)
                if (value MATCHES "[?&]${key}=([^&]+)")
                    list(APPEND secrets "${CMAKE_MATCH_1}")
                endif ()
            endforeach ()
        endif ()
    endforeach ()
endforeach ()

set(checked "")
foreach (s IN LISTS secrets)
    string(LENGTH "${s}" len)
    if (len GREATER_EQUAL 4)
        string(HEX "${s}" hex)
        list(APPEND checked "${hex}")
    endif ()
endforeach ()
list(REMOVE_DUPLICATES checked)
list(LENGTH checked secret_count)
if (sources_with_content GREATER 0 AND secret_count EQUAL 0)
    # fail closed: a secrets file exists but nothing could be extracted from it
    message(FATAL_ERROR "CREDENTIAL GUARD: secret source file(s) exist but no secret could be extracted - "
            "refusing to build a production PKG without a working credential scan")
endif ()

set(targets ${packaged} ${SCAN_FILES})
set(scanned 0)
foreach (f IN LISTS targets)
    if (NOT EXISTS "${f}" OR IS_DIRECTORY "${f}")
        continue()
    endif ()
    file(READ "${f}" content HEX)
    math(EXPR scanned "${scanned} + 1")
    set(index 0)
    foreach (hex IN LISTS checked)
        math(EXPR index "${index} + 1")
        string(FIND "${content}" "${hex}" pos)
        if (NOT pos EQUAL -1)
            # an odd hex offset is not a byte boundary: real matches are at even positions
            math(EXPR odd "${pos} % 2")
            if (odd EQUAL 0)
                message(FATAL_ERROR "CREDENTIAL GUARD: ${f} contains private secret #${index} (value not shown)")
            endif ()
        endif ()
    endforeach ()
endforeach ()

message(STATUS "credential guard: ${scanned} file(s) scanned against ${secret_count} private secret(s): clean")
