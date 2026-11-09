add_library(headerscope_graph STATIC graph.cpp reports.cpp)
add_library(HeaderScope::graph ALIAS headerscope_graph)
target_include_directories(headerscope_graph PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(headerscope_graph PUBLIC HeaderScope::contract)
