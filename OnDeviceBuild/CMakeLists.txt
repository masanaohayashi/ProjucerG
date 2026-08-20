cmake_minimum_required(VERSION 3.20)
project(OnDeviceBuild C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(OnDeviceBuildMachO STATIC src/MachOCheck.cpp)
target_include_directories(OnDeviceBuildMachO PUBLIC include)

add_executable(ondevice-test-macho tests/test_macho.cpp)
target_link_libraries(ondevice-test-macho PRIVATE OnDeviceBuildMachO)
