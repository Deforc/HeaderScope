if(NOT HEADERSCOPE_BUILD_SCANNER)
    add_library(headerscope_scanner INTERFACE)
    add_library(HeaderScope::scanner ALIAS headerscope_scanner)
    target_include_directories(headerscope_scanner INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(headerscope_scanner INTERFACE HeaderScope::contract)
    return()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG HINTS "${LLVM_DIR}/../clang")
if(LLVM_VERSION_MAJOR LESS 18)
    message(FATAL_ERROR "HeaderScope scanner requires LLVM/Clang 18 or newer")
endif()

# LibTooling is embedded in our executable, so its default resource lookup
# relative to that executable would miss Clang's builtin stddef.h/stdarg.h.
set(HEADERSCOPE_CLANG_RESOURCE_HINTS)
foreach(HEADERSCOPE_LLVM_LIBRARY_DIR IN LISTS LLVM_LIBRARY_DIRS)
    list(APPEND HEADERSCOPE_CLANG_RESOURCE_HINTS
        "${HEADERSCOPE_LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}/include"
        "${HEADERSCOPE_LLVM_LIBRARY_DIR}/clang/${LLVM_PACKAGE_VERSION}/include")
endforeach()
find_path(HEADERSCOPE_CLANG_RESOURCE_INCLUDE stddef.h
    HINTS ${HEADERSCOPE_CLANG_RESOURCE_HINTS} NO_DEFAULT_PATH REQUIRED)
get_filename_component(HEADERSCOPE_CLANG_RESOURCE_DIR
    "${HEADERSCOPE_CLANG_RESOURCE_INCLUDE}" DIRECTORY)

add_library(headerscope_scanner STATIC scanner.cpp)
add_library(HeaderScope::scanner ALIAS headerscope_scanner)
target_include_directories(headerscope_scanner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(headerscope_scanner SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(HEADERSCOPE_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(headerscope_scanner PRIVATE ${HEADERSCOPE_LLVM_DEFINITIONS})
target_compile_definitions(headerscope_scanner PRIVATE
    HEADERSCOPE_CLANG_RESOURCE_DIR="${HEADERSCOPE_CLANG_RESOURCE_DIR}")
target_link_libraries(headerscope_scanner PUBLIC HeaderScope::contract
    PRIVATE clangTooling clangFrontend clangLex clangBasic)

add_executable(headerscope_scan cli.cpp)
target_link_libraries(headerscope_scan PRIVATE HeaderScope::scanner)

if(BUILD_TESTING)
    add_executable(headerscope_scanner_tests tests/scanner_tests.cpp)
    target_link_libraries(headerscope_scanner_tests PRIVATE HeaderScope::scanner HeaderScope::demo_fixture)
    target_compile_definitions(headerscope_scanner_tests PRIVATE
        HEADERSCOPE_DEMO_DIRECTORY="${PROJECT_SOURCE_DIR}/demo/include-chain")
    add_test(NAME scanner COMMAND headerscope_scanner_tests)
endif()
