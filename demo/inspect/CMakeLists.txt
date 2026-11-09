add_executable(headerscope_inspect main.cpp)
target_link_libraries(headerscope_inspect PRIVATE HeaderScope::scanner HeaderScope::graph)
