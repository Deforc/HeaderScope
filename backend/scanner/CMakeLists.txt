if(HEADERSCOPE_BUILD_SCANNER)
    find_path(HEADERSCOPE_LIBCLANG_INCLUDE_DIR NAMES clang-c/Index.h REQUIRED)
    find_library(HEADERSCOPE_LIBCLANG_LIBRARY NAMES clang libclang REQUIRED)
    add_library(headerscope_scanner STATIC scanner.cpp)
    target_include_directories(headerscope_scanner
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${HEADERSCOPE_LIBCLANG_INCLUDE_DIR}"
    )
    target_link_libraries(headerscope_scanner
        PUBLIC HeaderScope::contract
        PRIVATE "${HEADERSCOPE_LIBCLANG_LIBRARY}"
    )
else()
    add_library(headerscope_scanner INTERFACE)
    target_link_libraries(headerscope_scanner INTERFACE HeaderScope::contract)
endif()

add_library(HeaderScope::scanner ALIAS headerscope_scanner)
