find_package(LLVM REQUIRED CONFIG)
add_executable(headerscope_inspect main.cpp snapshot.cpp)
target_compile_definitions(headerscope_inspect PRIVATE HEADERSCOPE_VERSION="${PROJECT_VERSION}")
target_include_directories(headerscope_inspect SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(headerscope_inspect PRIVATE HeaderScope::scanner HeaderScope::graph LLVMSupport)

if(BUILD_TESTING)
    add_executable(headerscope_report_tests tests.cpp snapshot.cpp)
    target_include_directories(headerscope_report_tests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_link_libraries(headerscope_report_tests PRIVATE HeaderScope::graph LLVMSupport)
    add_test(NAME reports COMMAND headerscope_report_tests)

    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    set(inspect_e2e_runner "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.py")
    add_test(NAME inspect_e2e
        COMMAND Python3::Interpreter "${inspect_e2e_runner}" workflow
            "$<TARGET_FILE:headerscope_inspect>" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME inspect_e2e.graphs
        COMMAND Python3::Interpreter "${inspect_e2e_runner}" graphs
            "$<TARGET_FILE:headerscope_inspect>" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(inspect_e2e inspect_e2e.graphs PROPERTIES TIMEOUT 60)

    add_test(NAME export_cli_arguments COMMAND ${CMAKE_COMMAND}
        "-DINSPECT=$<TARGET_FILE:headerscope_inspect>"
        "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/export-cli-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/export_cli_tests.cmake")
endif()
