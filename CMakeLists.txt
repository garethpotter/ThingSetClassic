# ThingSet Monitor for classic Mac OS (System 7.1+ with Open Transport).
#
# Builds for one architecture, chosen by the Retro68 toolchain file:
#   68k:      .../toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   PowerPC:  .../toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
# For a fat application containing both, build fat/ instead (see README).

cmake_minimum_required(VERSION 3.13)

project(ThingSetMonitor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
        # -Os leaves references to std::string members that Retro68's
        # PowerPC libstdc++ doesn't provide
        add_compile_options(-O2)
    else()
        add_compile_options(-Os)
    endif()
endif()

set(THINGSETPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../thingsetplusplus" CACHE PATH "Path to a ThingSet++ checkout")

if(NOT EXISTS "${THINGSETPP_DIR}/include/thingset++/ThingSet.hpp")
    message(FATAL_ERROR "ThingSet++ not found at ${THINGSETPP_DIR}; set -DTHINGSETPP_DIR=...")
endif()

# 68k is big-endian, so zcbor must byte-swap floats and ints.
add_library(zcbor STATIC
    ${THINGSETPP_DIR}/zcbor/src/zcbor_common.c
    ${THINGSETPP_DIR}/zcbor/src/zcbor_decode.c
    ${THINGSETPP_DIR}/zcbor/src/zcbor_encode.c)
target_include_directories(zcbor PUBLIC ${THINGSETPP_DIR}/zcbor/include)
target_compile_definitions(zcbor PUBLIC ZCBOR_BIG_ENDIAN ZCBOR_CANONICAL)

# Only ThingSet++'s binary encoding and decoding, and UDP report reassembly.
# The library's own socket/asio transports need POSIX sockets and threads,
# so Open Transport support lives in this project instead.
add_library(thingset++ STATIC
    ${THINGSETPP_DIR}/src/ThingSetDecoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetBinaryDecoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetEncoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetBinaryEncoder.cpp
    ${THINGSETPP_DIR}/src/ip/StreamingUdpThingSetBinaryDecoder.cpp)
target_include_directories(thingset++ PUBLIC ${THINGSETPP_DIR}/include)
target_link_libraries(thingset++ PUBLIC zcbor)

add_application(ThingSetMonitor
    src/main.cpp
    src/ListWindow.cpp
    src/TableWindow.cpp
    src/ClientWindow.cpp
    src/DeviceBrowser.cpp
    src/OpenTransportSubscriptionTransport.cpp
    src/OpenTransportTcpConnection.cpp
    src/ReportFormatter.cpp
    src/ThingSetMonitor.r
    TYPE APPL
    CREATOR TSmn)

if(CMAKE_SYSTEM_NAME MATCHES RetroPPC)
    # static glue plus import libraries for Open Transport's shared libraries
    target_link_libraries(ThingSetMonitor thingset++
        OpenTransportAppPPC OpenTptInetPPC OpenTransportLib OpenTptInternetLib)
else()
    target_link_libraries(ThingSetMonitor thingset++ OpenTransportApp OpenTransport OpenTptInet)
endif()

target_compile_options(ThingSetMonitor PRIVATE -ffunction-sections -Wall)

# Rez leaves the Finder's "has bundle" flag clear, without which the Finder
# ignores the icon (see tools/set-bundle-bit.py)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET ThingSetMonitor_APPL POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/set-bundle-bit.py
            ThingSetMonitor.bin ThingSetMonitor.dsk ThingSetMonitor.APPL %ThingSetMonitor.ad
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM)
set_target_properties(ThingSetMonitor PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
