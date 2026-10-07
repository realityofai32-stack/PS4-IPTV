# Build-time check of the private stream configuration before it is packaged into the PKG.
# Usage: cmake -DSTREAMS_FILE=<path> -P check_test_streams.cmake
# Never prints any value from the file (it contains credentials).

if (NOT EXISTS "${STREAMS_FILE}")
    message(FATAL_ERROR "PS4 playback test: ${STREAMS_FILE} not found.\n"
            "Copy config/test_streams.example.txt to config/test_streams.txt and fill in TS= and HLS=.")
endif ()

file(STRINGS "${STREAMS_FILE}" lines ENCODING UTF-8)
set(ts_count 0)
set(hls_count 0)
foreach (line IN LISTS lines)
    string(STRIP "${line}" line)
    if (line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif ()
    if (line MATCHES "^[Tt][Ss][ \t]*=[ \t]*(.*)$")
        set(value "${CMAKE_MATCH_1}")
        math(EXPR ts_count "${ts_count} + 1")
        set(key TS)
    elseif (line MATCHES "^[Hh][Ll][Ss][ \t]*=[ \t]*(.*)$")
        set(value "${CMAKE_MATCH_1}")
        math(EXPR hls_count "${hls_count} + 1")
        set(key HLS)
    else ()
        continue()
    endif ()
    if (value MATCHES "<" OR NOT value MATCHES "^[\"']?[a-zA-Z]+://")
        message(FATAL_ERROR "PS4 playback test: ${key}= in ${STREAMS_FILE} is not a filled-in URL")
    endif ()
    if (value MATCHES "^[\"']?[Hh][Tt][Tt][Pp][Ss]://")
        message(WARNING "PS4 playback test: ${key} uses https, which pPlay's FFmpeg build cannot open")
    endif ()
endforeach ()

if (NOT ts_count EQUAL 1 OR NOT hls_count EQUAL 1)
    message(FATAL_ERROR "PS4 playback test: ${STREAMS_FILE} needs exactly one TS= and one HLS= line "
            "(found TS x${ts_count}, HLS x${hls_count})")
endif ()
message(STATUS "PS4 playback test: ${STREAMS_FILE}: TS and HLS present (values not shown)")
