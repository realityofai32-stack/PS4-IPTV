# PS4 (OpenOrbis / PacBrew) CMake toolchain file for a native Windows host.
#
# Port of PacBrew's ps4.cmake (PacBrew/ps4-openorbis: vars/ps4.cmake, pkg ps4-openorbis-vars 1.1-3),
# which is what pPlay / libcross2d expect at ${OPENORBIS}/cmake/ps4.cmake. Compiler flags, link
# line, create-fself arguments and param.sfo entries are kept identical to the original; only the
# host side changed:
#   - clang / ld.lld / llvm-ar come from the LLVM 12.0.1 Windows build (toolchain/llvm-12.0.1)
#   - create-fself / create-gp4 / PkgTool.Core are Windows executables (toolchain/host-bin)
#   - pkg-config is pkgconf.exe; PacBrew's bash wrapper is replaced by environment variables
#     set here, with PKG_CONFIG_SYSROOT_DIR mapping the packages' /opt/pacbrew/... paths
#   - PkgTool.Core targets .NET Core 3.0; it is run on the installed .NET runtime via
#     DOTNET_ROLL_FORWARD=Major
#
# Expected layout is the one produced by scripts/setup-toolchain.ps1.

cmake_minimum_required(VERSION 3.22)

get_filename_component(PS4_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PS4_TOOLCHAIN_ROOT "${PS4_REPO_ROOT}/toolchain" CACHE PATH "project-local toolchain directory")
set(PS4_LLVM_ROOT "${PS4_TOOLCHAIN_ROOT}/llvm-12.0.1" CACHE PATH "LLVM 12.0.1 install directory")
set(PS4_HOST_BIN "${PS4_TOOLCHAIN_ROOT}/host-bin" CACHE PATH "create-fself/create-gp4/PkgTool.Core directory")
set(PS4_PACBREW_SYSROOT "${PS4_TOOLCHAIN_ROOT}/pacbrew" CACHE PATH "root containing opt/pacbrew/ps4/openorbis")

###################################################################

if (NOT DEFINED ENV{OPENORBIS})
    set(OPENORBIS "${PS4_PACBREW_SYSROOT}/opt/pacbrew/ps4/openorbis")
    set(ENV{OPENORBIS} ${OPENORBIS})
else ()
    file(TO_CMAKE_PATH "$ENV{OPENORBIS}" OPENORBIS)
endif ()

if (NOT DEFINED ENV{OO_PS4_TOOLCHAIN})
    set(OO_PS4_TOOLCHAIN ${OPENORBIS})
else ()
    file(TO_CMAKE_PATH "$ENV{OO_PS4_TOOLCHAIN}" OO_PS4_TOOLCHAIN)
endif ()

foreach (_p "${OPENORBIS}/link.x" "${PS4_LLVM_ROOT}/bin/clang.exe" "${PS4_HOST_BIN}/create-fself.exe")
    if (NOT EXISTS "${_p}")
        message(FATAL_ERROR "PS4 toolchain: missing ${_p} - run scripts/setup-toolchain.ps1")
    endif ()
endforeach ()

list(APPEND CMAKE_MODULE_PATH "${OPENORBIS}/cmake")

set(PS4 TRUE)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(TARGET x86_64-pc-freebsd-elf)
set(CMAKE_SYSTEM_VERSION 12)
set(CMAKE_CROSSCOMPILING 1)

set(CMAKE_ASM_COMPILER ${PS4_LLVM_ROOT}/bin/clang.exe CACHE PATH "")
set(CMAKE_C_COMPILER ${PS4_LLVM_ROOT}/bin/clang.exe CACHE PATH "")
set(CMAKE_CXX_COMPILER ${PS4_LLVM_ROOT}/bin/clang++.exe CACHE PATH "")
set(CMAKE_LINKER ${PS4_LLVM_ROOT}/bin/ld.lld.exe CACHE PATH "")
set(CMAKE_AR ${PS4_LLVM_ROOT}/bin/llvm-ar.exe CACHE PATH "")
set(CMAKE_RANLIB ${PS4_LLVM_ROOT}/bin/llvm-ranlib.exe CACHE PATH "")
set(CMAKE_STRIP ${PS4_LLVM_ROOT}/bin/llvm-strip.exe CACHE PATH "")
set(CMAKE_NM ${PS4_LLVM_ROOT}/bin/llvm-nm.exe CACHE PATH "")
set(CMAKE_OBJCOPY ${PS4_LLVM_ROOT}/bin/llvm-objcopy.exe CACHE PATH "")

# Windows host: make sure CMake's compiler identification also targets the PS4 triple instead of
# clang's default x86_64-pc-windows-msvc (on Linux the default triple already is an ELF one).
set(CMAKE_C_COMPILER_TARGET x86_64-pc-freebsd12-elf)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-freebsd12-elf)
set(CMAKE_ASM_COMPILER_TARGET x86_64-pc-freebsd12-elf)

# We use the linker directly instead of using the llvm wrapper.
# CMake uses `-Xlinker` for passing llvm linker flags
# added via `add_link_options(... "LINKER:...")`.
# Force the correct linker flag generation:
macro(reset_linker_wrapper_flag)
    set(CMAKE_ASM_LINKER_WRAPPER_FLAG "")
    set(CMAKE_C_LINKER_WRAPPER_FLAG "")
    set(CMAKE_CXX_LINKER_WRAPPER_FLAG "")
endmacro()
variable_watch(CMAKE_ASM_LINKER_FLAG reset_linker_wrapper_flag)
variable_watch(CMAKE_C_LINKER_WRAPPER_FLAG reset_linker_wrapper_flag)
variable_watch(CMAKE_CXX_LINKER_WRAPPER_FLAG reset_linker_wrapper_flag)

set(CMAKE_LIBRARY_ARCHITECTURE x86_64 CACHE INTERNAL "abi")

set(CMAKE_FIND_ROOT_PATH ${OPENORBIS} ${OPENORBIS}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Shared libs not available")

###################################################################

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_ASM_FLAGS_INIT
  "-target x86_64-pc-freebsd12-elf \
   -D__PS4__ -D__OPENORBIS__ -D__ORBIS__ \
   -DPS4 -D__BSD_VISIBLE -D_BSD_SOURCE \
   -fPIC -funwind-tables \
   -isysroot ${OPENORBIS} -isystem ${OPENORBIS}/include \
   -I${OPENORBIS}/usr/include")

set(CMAKE_C_FLAGS_INIT "${CMAKE_ASM_FLAGS_INIT}")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -I${OPENORBIS}/include/c++/v1")

set(CMAKE_C_STANDARD_LIBRARIES "-lkernel -lc -lclang_rt.builtins-x86_64 -lSceLibcInternal")
set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_C_STANDARD_LIBRARIES} -lc++")

set(CMAKE_EXE_LINKER_FLAGS_INIT
  "-m elf_x86_64 -pie --eh-frame-hdr \
   --script ${OPENORBIS}/link.x \
   -L${OPENORBIS}/lib -L${OPENORBIS}/usr/lib")

# crt1.o may be already added to LDFLAGS from "ps4vars.sh", so remove LDFLAGS env (todo: find a better way...)
set(ENV{LDFLAGS} "" CACHE STRING FORCE)

set(CMAKE_ASM_LINK_EXECUTABLE
  "<CMAKE_LINKER> -o <TARGET> <CMAKE_ASM_LINK_FLAGS> <LINK_FLAGS> --start-group \
   <OBJECTS> <LINK_LIBRARIES> --end-group")

set(CMAKE_C_LINK_EXECUTABLE
  "<CMAKE_LINKER> -o <TARGET> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> \
  --start-group \
     ${OPENORBIS}/lib/crt1.o ${OPENORBIS}/lib/crti.o \
     <OBJECTS> <LINK_LIBRARIES> \
     ${OPENORBIS}/lib/crtn.o \
  --end-group")

set(CMAKE_CXX_LINK_EXECUTABLE
  "<CMAKE_LINKER> -o <TARGET> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> \
  --start-group \
     ${OPENORBIS}/lib/crt1.o ${OPENORBIS}/lib/crti.o \
     <OBJECTS> <LINK_LIBRARIES> \
     ${OPENORBIS}/lib/crtn.o \
  --end-group")

# Start find_package in config mode
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

# pkg-config: equivalent of PacBrew's "openorbis-pkg-config" bash wrapper
set(PKG_CONFIG_EXECUTABLE "${PS4_TOOLCHAIN_ROOT}/pkgconf/pkgconf.exe" CACHE FILEPATH "pkg-config executable")
set(PKG_CONFIG_ARGN "--static" CACHE STRING "Arguments to supply to pkg-config")
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "${OPENORBIS}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${PS4_PACBREW_SYSROOT}")

function(add_self project)
    set(AUTH_INFO "000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000")
    add_custom_command(
            OUTPUT "${project}.self"
            COMMAND ${CMAKE_COMMAND} -E env "OO_PS4_TOOLCHAIN=${OPENORBIS}" "${PS4_HOST_BIN}/create-fself.exe" "-in=${project}" "-out=${project}.oelf" "--eboot" "eboot.bin" "--paid" "0x3800000000000035" "--authinfo" "${AUTH_INFO}"
            VERBATIM
            DEPENDS "${project}"
    )
    add_custom_target(
            "${project}_self" ALL
            DEPENDS "${project}.self"
    )
endfunction()

function(add_pkg project pkgdir title-id title version)
    # Title must not exceed 128 characters
    string(SUBSTRING "${title}" 0 127 title)

    # Format version string in such a way that is acceptable by the PS4
    string(SUBSTRING "${version}" 0 7 verclean)
    string(REGEX MATCH "([0-9]+\\.[0-9]+)" verclean ${verclean})
    if("${verclean}" STREQUAL "")
        message(WARNING "The version string '${version}' is formatted in a way that is incompatable with the PS4, using '01.00'")
        set(verclean "01.00")
    endif()

    # Format content-id based on title-id and version
    string(REPLACE "." "0" vercont ${verclean})
    string(APPEND vercont "00000000")
    string(SUBSTRING "${vercont}" 0 7 vercont)
    set(content_id "IV0001-${title-id}_00-${title-id}${vercont}")
    # export pkg name for end user
    set(PKG_OUT_NAME "${content_id}.pkg" CACHE STRING "ps4 pkg name" FORCE)

    set(attribute 0)
    if(${ARGC} GREATER 5)
        set(attribute ${ARGV5})
    endif()

    set(category "gde")
    if(${ARGC} GREATER 6)
        set(category "${ARGV6}")
    endif()

    set(PKGTOOL "${PS4_HOST_BIN}/PkgTool.Core.exe" CACHE STRING "PKGTOOL" FORCE)
    set(DOTFIX "DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1" "DOTNET_ROLL_FORWARD=Major")

    add_custom_command(
            OUTPUT "${project}.pkg"
            # copy required files to binary directory
            COMMAND ${CMAKE_COMMAND} -E copy eboot.bin ${pkgdir}/eboot.bin
            # generate sfo
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_new ${pkgdir}/sce_sys/param.sfo
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value "${verclean}"
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value ${attribute}
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value "${category}"
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo FORMAT --type Utf8 --maxsize 4 --value "obs"
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "${content_id}"
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 1020
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "${title}"
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "${title-id}"
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} sfo_setentry ${pkgdir}/sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value "${verclean}"
            # generate gp4 file
            COMMAND "${PS4_HOST_BIN}/create-gp4.exe" -out ${pkgdir}/${project}.gp4 --content-id "${content_id}" --path "${pkgdir}"
            # generate pkg
            COMMAND ${CMAKE_COMMAND} -E env ${DOTFIX} ${PKGTOOL} pkg_build ${pkgdir}/${project}.gp4 ${CMAKE_BINARY_DIR}
            # cleanup
            COMMAND ${CMAKE_COMMAND} -E remove ${pkgdir}/eboot.bin
            COMMAND ${CMAKE_COMMAND} -E remove ${pkgdir}/sce_sys/param.sfo
            COMMAND ${CMAKE_COMMAND} -E remove ${pkgdir}/${project}.gp4
            VERBATIM
            DEPENDS "${project}.self"
    )
    add_custom_target(
            "${project}_pkg" ALL
            DEPENDS "${project}.pkg"
    )
endfunction()
