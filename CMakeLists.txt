cmake_minimum_required(VERSION 3.16)
project(mprpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Debug
      CACHE STRING "Choose the build type." FORCE)
  set_property(
    CACHE CMAKE_BUILD_TYPE
    PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(MPRPC_BUILD_EXAMPLES "Build the example provider and consumer programs" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/lib)

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

if(TARGET protobuf::libprotobuf)
  set(MPRPC_PROTOBUF_TARGET protobuf::libprotobuf)
else()
  set(MPRPC_PROTOBUF_TARGET ${Protobuf_LIBRARIES})
endif()

add_subdirectory(src)

if(MPRPC_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()
