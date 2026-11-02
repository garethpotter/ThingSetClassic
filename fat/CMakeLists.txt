# Builds ThingSet Monitor as a fat application: 68k code in CODE resources
# for 68k Macs, and a PowerPC code fragment in the data fork (found through a
# 'cfrg' resource) for Power Macs.
#
# cmake -S fat -B build-fat -DRETRO68_TOOLCHAIN=path/to/Retro68-build/toolchain
# cmake --build build-fat

cmake_minimum_required(VERSION 3.13)

project(ThingSetMonitorFat NONE)

include(ExternalProject)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(RETRO68_TOOLCHAIN "" CACHE PATH "Path to Retro68-build/toolchain")
set(THINGSETPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../thingsetplusplus" CACHE PATH "Path to a ThingSet++ checkout")

if(NOT EXISTS "${RETRO68_TOOLCHAIN}/bin/Rez")
    message(FATAL_ERROR "Retro68 not found; set -DRETRO68_TOOLCHAIN=path/to/Retro68-build/toolchain")
endif()

set(name ThingSetMonitor)
set(m68k_dir "${CMAKE_BINARY_DIR}/m68k")
set(ppc_dir "${CMAKE_BINARY_DIR}/ppc")

ExternalProject_Add(m68k
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.."
    BINARY_DIR "${m68k_dir}"
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${RETRO68_TOOLCHAIN}/m68k-apple-macos/cmake/retro68.toolchain.cmake
        -DTHINGSETPP_DIR=${THINGSETPP_DIR}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${m68k_dir}/${name}.code.bin" "${m68k_dir}/${name}.r.rsrc.bin")

ExternalProject_Add(ppc
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.."
    BINARY_DIR "${ppc_dir}"
    CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${RETRO68_TOOLCHAIN}/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
        -DTHINGSETPP_DIR=${THINGSETPP_DIR}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${ppc_dir}/${name}.pef")

# As each architecture's own packaging does, but both at once: the 'cfrg'
# from Retro68's PowerPC template, the 68k CODE resources, the PEF as the
# data fork, and then the application's resources, whose SIZE overrides the
# template's.
set(rincludes "${RETRO68_TOOLCHAIN}/RIncludes")
add_custom_command(
    OUTPUT ${name}.bin ${name}.dsk ${name}.APPL ${name}.ad "%${name}.ad"
    COMMAND "${RETRO68_TOOLCHAIN}/bin/Rez"
            "${rincludes}/RetroPPCAPPL.r"
            "-I${rincludes}"
            "-DCFRAG_NAME=\"${name}\""
            --copy "${m68k_dir}/${name}.code.bin"
            --data "${ppc_dir}/${name}.pef"
            "${m68k_dir}/${name}.r.rsrc.bin"
            -o ${name}.bin
            -t APPL -c TSmn
            --cc ${name}.dsk --cc ${name}.APPL --cc "%${name}.ad"
    # Rez leaves the Finder's "has bundle" flag clear, without which the
    # Finder ignores the icon
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/../tools/set-bundle-bit.py"
            ${name}.bin ${name}.dsk ${name}.APPL "%${name}.ad"
    DEPENDS m68k ppc
            "${m68k_dir}/${name}.code.bin" "${m68k_dir}/${name}.r.rsrc.bin" "${ppc_dir}/${name}.pef"
    VERBATIM)

add_custom_target(${name} ALL DEPENDS ${name}.bin)
