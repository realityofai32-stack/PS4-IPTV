# app_pkg_version(<version> <out-var>): the PS4 APP_VER / VERSION ("XX.YY") of an application version
# MAJOR.MINOR.PATCH: MAJOR as two digits, then MINOR and PATCH as one digit each (0.3.0 -> 00.30, 0.4.0 -> 00.40,
# 1.2.3 -> 01.23). Anything else stops the configuration: a version the console's XX.YY cannot hold is never
# packaged under a different number. Tested by tests/version/test-app-version.sh.
function(app_pkg_version version out)
    if (NOT version MATCHES "^([0-9]|[1-9][0-9])\\.([0-9])\\.([0-9])$")
        message(FATAL_ERROR "APP_VERSION '${version}' must be MAJOR.MINOR.PATCH with MAJOR 0-99 and one-digit MINOR "
                "and PATCH (the PS4 APP_VER has room for XX.YY only)")
    endif ()
    set(major "${CMAKE_MATCH_1}")
    if (major LESS 10)
        set(major "0${major}")
    endif ()
    set(${out} "${major}.${CMAKE_MATCH_2}${CMAKE_MATCH_3}" PARENT_SCOPE)
endfunction()
