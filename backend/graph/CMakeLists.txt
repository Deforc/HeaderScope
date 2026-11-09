# Replace INTERFACE with a compiled library when the graph is implemented.
add_library(headerscope_graph INTERFACE)
add_library(HeaderScope::graph ALIAS headerscope_graph)
target_link_libraries(headerscope_graph INTERFACE HeaderScope::contract)
