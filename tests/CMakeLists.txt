# Host-side tests (build with the native compiler, not Retro68):
#
# cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests

cmake_minimum_required(VERSION 3.13)

project(ThingSetMonitorTests LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THINGSETPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../thingsetplusplus" CACHE PATH "Path to a ThingSet++ checkout")

# ThingSet++ codec, plus its server for the client test to talk to
add_library(thingset++ STATIC
    ${THINGSETPP_DIR}/src/ThingSetDecoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetBinaryDecoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetEncoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetBinaryEncoder.cpp
    ${THINGSETPP_DIR}/src/ip/StreamingUdpThingSetBinaryDecoder.cpp
    ${THINGSETPP_DIR}/src/ThingSetServer.cpp
    ${THINGSETPP_DIR}/src/ThingSetRequestContext.cpp
    ${THINGSETPP_DIR}/src/ThingSetRegistry.cpp
    ${THINGSETPP_DIR}/src/ThingSetNode.cpp
    ${THINGSETPP_DIR}/src/ThingSetParentNode.cpp
    ${THINGSETPP_DIR}/src/Eui.cpp
    ${THINGSETPP_DIR}/zcbor/src/zcbor_common.c
    ${THINGSETPP_DIR}/zcbor/src/zcbor_decode.c
    ${THINGSETPP_DIR}/zcbor/src/zcbor_encode.c)
target_include_directories(thingset++ PUBLIC
    ${THINGSETPP_DIR}/include
    ${THINGSETPP_DIR}/zcbor/include)
target_compile_definitions(thingset++ PUBLIC ZCBOR_CANONICAL)

add_library(stubs STATIC stubs/OpenTransportStubs.cpp)
target_include_directories(stubs PUBLIC stubs ../src)

add_executable(HostDecodeTest
    HostDecodeTest.cpp
    ../src/OpenTransportSubscriptionTransport.cpp
    ../src/ReportFormatter.cpp)
target_link_libraries(HostDecodeTest stubs thingset++)

add_executable(HostClientTest
    HostClientTest.cpp
    ../src/DeviceBrowser.cpp
    ../src/OpenTransportTcpConnection.cpp
    ../src/ReportFormatter.cpp)
target_link_libraries(HostClientTest stubs thingset++)

enable_testing()
add_test(NAME HostDecodeTest COMMAND HostDecodeTest)
add_test(NAME HostClientTest COMMAND HostClientTest)
