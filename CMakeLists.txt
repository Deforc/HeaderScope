cmake_minimum_required(VERSION 3.21)
project(HeaderScope VERSION 0.1.1 LANGUAGES C CXX)

include(CTest)

option(HEADERSCOPE_BUILD_APP "Build the Qt desktop application" ON)
option(HEADERSCOPE_BUILD_SCANNER "Build the Clang LibTooling scanner and demo CLI" OFF)

add_library(headerscope_contract INTERFACE)
add_library(HeaderScope::contract ALIAS headerscope_contract)
target_compile_features(headerscope_contract INTERFACE cxx_std_20)
target_include_directories(headerscope_contract INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_subdirectory(backend/scanner)
add_subdirectory(backend/graph)
add_subdirectory(demo/fixtures)

if(HEADERSCOPE_BUILD_SCANNER)
    add_subdirectory(demo/inspect)
endif()

if(HEADERSCOPE_BUILD_APP)
    add_subdirectory(app)
endif()
