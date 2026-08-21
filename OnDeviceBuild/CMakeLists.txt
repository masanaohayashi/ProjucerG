cmake_minimum_required(VERSION 3.20)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()

project(OnDeviceBuild C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ONDEVICE_ENGINE_SOURCES
    src/MachOCheck.cpp src/Compiler.cpp src/Linker.cpp
    src/BundleBuilder.cpp src/Signer.cpp src/Pkcs12.cpp
    src/ZipStore.cpp src/Engine.cpp src/BuildRunner.mm)

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

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    option(ONDEVICE_IOS_SIMULATOR "Build the in-process engine for arm64 iOS Simulator" OFF)

    if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator" OR CMAKE_OSX_SYSROOT MATCHES "iPhoneSimulator")
        set(ONDEVICE_IOS_SIMULATOR ON CACHE BOOL "Build the in-process engine for arm64 iOS Simulator" FORCE)
    elseif(CMAKE_OSX_SYSROOT MATCHES "iphoneos" OR CMAKE_OSX_SYSROOT MATCHES "iPhoneOS")
        set(ONDEVICE_IOS_SIMULATOR OFF CACHE BOOL "Build the in-process engine for arm64 iOS Simulator" FORCE)
    endif()

    if(ONDEVICE_IOS_SIMULATOR)
        set(_ondevice_llvm_default "$ENV{HOME}/Documents/src/PocClangIOS/build-ios-simulator/llvm-ios-simulator.a")
        set(_ondevice_openssl_default "$ENV{HOME}/Documents/src/PocSignIOS/openssl-ios-simulator")
        set(_ondevice_llvm_dir_default "$ENV{HOME}/Documents/src/PocClangIOS/build-ios-simulator")
    else()
        set(_ondevice_llvm_default "$ENV{HOME}/Documents/src/PocClangIOS/build-ios/llvm-ios.a")
        set(_ondevice_openssl_default "$ENV{HOME}/Documents/src/PocSignIOS/openssl-ios")
        set(_ondevice_llvm_dir_default "$ENV{HOME}/Documents/src/PocClangIOS/build-ios")
    endif()

    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
    endif()

    set(ONDEVICE_LLVM_IOS_ARCHIVE "${_ondevice_llvm_default}"
        CACHE FILEPATH "Combined LLVM/Clang/lld archive for iOS")
    set(ONDEVICE_OPENSSL_IOS "${_ondevice_openssl_default}"
        CACHE PATH "OpenSSL built for iOS")
    set(ONDEVICE_LLVM_IOS_DIR "${_ondevice_llvm_dir_default}"
        CACHE PATH "iOS LLVM build tree (generated headers)")
    set(ONDEVICE_LLVM_SRC "$ENV{HOME}/Documents/src/PocClangIOS/llvm-project"
        CACHE PATH "llvm-project source tree")

    if(NOT EXISTS "${ONDEVICE_LLVM_IOS_ARCHIVE}")
        message(FATAL_ERROR "iOS LLVM archive not found: ${ONDEVICE_LLVM_IOS_ARCHIVE}")
    endif()
    if(NOT EXISTS "${ONDEVICE_OPENSSL_IOS}/include/openssl/crypto.h")
        message(FATAL_ERROR "iOS OpenSSL not found: ${ONDEVICE_OPENSSL_IOS}")
    endif()

    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO)
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")

    add_library(crypto STATIC IMPORTED)
    set_target_properties(crypto PROPERTIES
        IMPORTED_LOCATION "${ONDEVICE_OPENSSL_IOS}/lib/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ONDEVICE_OPENSSL_IOS}/include")
    add_library(ssl STATIC IMPORTED)
    set_target_properties(ssl PROPERTIES
        IMPORTED_LOCATION "${ONDEVICE_OPENSSL_IOS}/lib/libssl.a"
        INTERFACE_INCLUDE_DIRECTORIES "${ONDEVICE_OPENSSL_IOS}/include")

    add_library(zsign STATIC ${ZSIGN_SOURCES} ${ZSIGN_THIRD_PARTY})
    target_include_directories(zsign PRIVATE ${ZSIGN_PRIVATE_INCLUDES} ${ONDEVICE_OPENSSL_IOS}/include)
    set_target_properties(zsign PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
    target_compile_options(zsign PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++11>
        $<$<COMPILE_LANGUAGE:C>:-std=c11>)
    target_link_libraries(zsign PUBLIC ssl crypto)

    add_library(OnDeviceBuild STATIC ${ONDEVICE_ENGINE_SOURCES})
    target_include_directories(OnDeviceBuild PUBLIC include
        PRIVATE
            ${ONDEVICE_LLVM_SRC}/llvm/include
            ${ONDEVICE_LLVM_IOS_DIR}/include
            ${ONDEVICE_LLVM_SRC}/clang/include
            ${ONDEVICE_LLVM_IOS_DIR}/tools/clang/include
            ${ONDEVICE_LLVM_SRC}/lld/include)
    set_property(SOURCE src/Signer.cpp src/BundleBuilder.cpp
        APPEND PROPERTY INCLUDE_DIRECTORIES ${ZSIGN_PRIVATE_INCLUDES})
    target_compile_definitions(OnDeviceBuild PRIVATE
        __STDC_CONSTANT_MACROS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS)
    target_compile_options(OnDeviceBuild PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fno-rtti>)
    target_link_libraries(OnDeviceBuild PUBLIC
        ${ONDEVICE_LLVM_IOS_ARCHIVE}
        zsign
        "-framework Foundation")
    return()
endif()

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

add_library(OnDeviceBuild STATIC ${ONDEVICE_ENGINE_SOURCES})
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
