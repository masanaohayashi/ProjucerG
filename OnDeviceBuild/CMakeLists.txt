cmake_minimum_required(VERSION 3.20)
project(OnDeviceBuild C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(OnDeviceBuildMachO STATIC src/MachOCheck.cpp)
target_include_directories(OnDeviceBuildMachO PUBLIC include)

add_executable(ondevice-test-macho tests/test_macho.cpp)
target_link_libraries(ondevice-test-macho PRIVATE OnDeviceBuildMachO)

add_executable(ondevice-test-language tests/test_language.cpp)
target_include_directories(ondevice-test-language PRIVATE include)

set(ONDEVICE_LLVM_DIR "$ENV{HOME}/Documents/src/PocClangIOS/build-host"
    CACHE PATH "LLVM build dir with LLVMConfig.cmake")
find_package(LLVM REQUIRED CONFIG PATHS ${ONDEVICE_LLVM_DIR} NO_DEFAULT_PATH)
find_package(Clang REQUIRED CONFIG PATHS ${ONDEVICE_LLVM_DIR} NO_DEFAULT_PATH)

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

# lld does not export CMake imported targets in this LLVM build.
set(ONDEVICE_LLD_INCLUDE_DIR "${ONDEVICE_LLVM_DIR}/../llvm-project/lld/include"
    CACHE PATH "lld headers (source tree)")

# Host OpenSSL (Homebrew). Never the iOS openssl-ios tree.
if(NOT OPENSSL_ROOT_DIR)
    foreach(_ondevice_ssl /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3)
        if(EXISTS "${_ondevice_ssl}")
            set(OPENSSL_ROOT_DIR "${_ondevice_ssl}")
            break()
        endif()
    endforeach()
endif()
find_package(OpenSSL REQUIRED)
message(STATUS "Using OpenSSL ${OPENSSL_VERSION} from ${OPENSSL_INCLUDE_DIR}")

set(ZSIGN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zsign")
file(GLOB ZSIGN_SOURCES
    ${ZSIGN_ROOT}/src/*.cpp
    ${ZSIGN_ROOT}/src/common/*.cpp)
list(REMOVE_ITEM ZSIGN_SOURCES ${ZSIGN_ROOT}/src/zsign.cpp)
file(GLOB ZSIGN_THIRD_PARTY
    ${ZSIGN_ROOT}/src/third-party/zlib/*.c
    ${ZSIGN_ROOT}/src/third-party/minizip/*.c)
list(REMOVE_ITEM ZSIGN_THIRD_PARTY ${ZSIGN_ROOT}/src/third-party/minizip/iowin32.c)

set(ZSIGN_PRIVATE_INCLUDES
    ${ZSIGN_ROOT}/src
    ${ZSIGN_ROOT}/src/common
    ${ZSIGN_ROOT}/src/third-party/zlib
    ${ZSIGN_ROOT}/src/third-party/minizip)

add_library(zsign STATIC ${ZSIGN_SOURCES} ${ZSIGN_THIRD_PARTY})
target_include_directories(zsign PRIVATE ${ZSIGN_PRIVATE_INCLUDES} ${OPENSSL_INCLUDE_DIR})
set_target_properties(zsign PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON)
target_compile_options(zsign PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>
    $<$<COMPILE_LANGUAGE:C>:-std=c11>)
target_link_libraries(zsign PUBLIC OpenSSL::Crypto OpenSSL::SSL)

add_library(OnDeviceBuild STATIC
    src/MachOCheck.cpp src/Compiler.cpp src/Linker.cpp
    src/BundleBuilder.cpp src/Signer.cpp src/Pkcs12.cpp
    src/ZipStore.cpp src/Engine.cpp src/BuildRunner.mm)
target_include_directories(OnDeviceBuild PUBLIC include
    PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} ${ONDEVICE_LLD_INCLUDE_DIR})
set_property(SOURCE src/Signer.cpp src/BundleBuilder.cpp
    APPEND PROPERTY INCLUDE_DIRECTORIES ${ZSIGN_PRIVATE_INCLUDES})
target_compile_definitions(OnDeviceBuild PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(OnDeviceBuild PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)
target_link_libraries(OnDeviceBuild PRIVATE
    ${ONDEVICE_LLVM_DIR}/lib/liblldMachO.a
    ${ONDEVICE_LLVM_DIR}/lib/liblldCommon.a
    LLVMAArch64CodeGen LLVMAArch64AsmParser LLVMAArch64Desc
    LLVMAArch64Info LLVMAArch64Utils
    clangCodeGen clangFrontend clangSerialization clangDriver
    clangParse clangSema clangAnalysis clangEdit clangAST
    clangLex clangBasic clangSupport
    zsign)
target_link_libraries(OnDeviceBuild PUBLIC "-framework Foundation")

# Public-header tests must not inherit LLVM's -fno-rtti.
add_executable(ondevice-test-compile tests/test_compile.cpp)
target_link_libraries(ondevice-test-compile PRIVATE OnDeviceBuild)
target_compile_definitions(ondevice-test-compile PRIVATE
    ONDEVICE_FIXTURE_UIKIT="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/uikit.mm")

add_executable(ondevice-test-link tests/test_link.cpp)
target_link_libraries(ondevice-test-link PRIVATE OnDeviceBuild)
target_compile_definitions(ondevice-test-link PRIVATE
    ONDEVICE_FIXTURE_UIKIT="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/uikit.mm"
    ONDEVICE_FIXTURE_LINK_MAIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/link_main.cpp")

add_executable(ondevice-test-ipa tests/test_ipa.cpp)
target_link_libraries(ondevice-test-ipa PRIVATE OnDeviceBuild)
target_compile_definitions(ondevice-test-ipa PRIVATE
    ONDEVICE_FIXTURE_UIKIT="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/uikit.mm"
    ONDEVICE_FIXTURE_LINK_MAIN="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/link_main.cpp")

add_executable(ondevice-test-zipstore tests/test_zipstore.cpp)
target_link_libraries(ondevice-test-zipstore PRIVATE OnDeviceBuild)

add_executable(ondevice-test-engine tests/test_engine.cpp)
target_link_libraries(ondevice-test-engine PRIVATE OnDeviceBuild)
target_compile_definitions(ondevice-test-engine PRIVATE
    ONDEVICE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

add_executable(ondevice-build-host host/main.cpp)
target_link_libraries(ondevice-build-host PRIVATE OnDeviceBuild)
