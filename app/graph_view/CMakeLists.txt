# Replace INTERFACE with a compiled library when the widget is implemented.
add_library(headerscope_graph_view INTERFACE)
add_library(HeaderScope::graph_view ALIAS headerscope_graph_view)
target_link_libraries(headerscope_graph_view INTERFACE
    HeaderScope::graph
    Qt6::Widgets
)
