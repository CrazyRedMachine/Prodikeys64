cmake_minimum_required(VERSION 3.17)
project(prodikeys64)

set(CMAKE_CXX_STANDARD 14)

include_directories(${prodikeys64_SOURCE_DIR})
include_directories(${prodikeys64_SOURCE_DIR}/include)
link_directories(${PROJECT_SOURCE_DIR}/lib)

include_directories(.)
add_executable(prodikeys64 WIN32
        resource.h
        stdafx.cpp
        stdafx.h
        prodikeys64.rc
        prodikeys64.cpp
        prodikeys-core.cpp)
target_link_libraries(prodikeys64 libusb-1.0 teVirtualMIDI64)
