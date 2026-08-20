cmake_minimum_required(VERSION 3.20)
project(OnDeviceBuild C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(OnDeviceBuildMachO STATIC src/MachOCheck.cpp)
target_include_directories(OnDeviceBuildMachO PUBLIC include)

add_executable(ondevice-test-macho tests/test_macho.cpp)
target_link_libraries(ondevice-test-macho PRIVATE OnDeviceBuildMachO)

set(ONDEVICE_LLVM_DIR "$ENV{HOME}/Documents/src/PocClangIOS/build-host"
    CACHE PATH "LLVM build dir with LLVMConfig.cmake")
find_package(LLVM REQUIRED CONFIG PATHS ${ONDEVICE_LLVM_DIR} NO_DEFAULT_PATH)
find_package(Clang REQUIRED CONFIG PATHS ${ONDEVICE_LLVM_DIR} NO_DEFAULT_PATH)

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

# lld does not export CMake imported targets in this LLVM build.
set(ONDEVICE_LLD_INCLUDE_DIR "${ONDEVICE_LLVM_DIR}/../llvm-project/lld/include"
    CACHE PATH "lld headers (source tree)")

add_library(OnDeviceBuild STATIC src/MachOCheck.cpp src/Compiler.cpp src/Linker.cpp)
target_include_directories(OnDeviceBuild PUBLIC include
    PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} ${ONDEVICE_LLD_INCLUDE_DIR})
target_compile_definitions(OnDeviceBuild PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(OnDeviceBuild PRIVATE -fno-rtti)
target_link_libraries(OnDeviceBuild PRIVATE
    ${ONDEVICE_LLVM_DIR}/lib/liblldMachO.a
    ${ONDEVICE_LLVM_DIR}/lib/liblldCommon.a
    LLVMAArch64CodeGen LLVMAArch64AsmParser LLVMAArch64Desc
    LLVMAArch64Info LLVMAArch64Utils
    clangCodeGen clangFrontend clangSerialization clangDriver
    clangParse clangSema clangAnalysis clangEdit clangAST
    clangLex clangBasic clangSupport)

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
