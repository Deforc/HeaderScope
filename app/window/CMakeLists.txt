qt_add_executable(headerscope_app WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(headerscope_app PRIVATE
    HeaderScope::scanner
    HeaderScope::graph_view
    Qt6::Widgets
)
set_target_properties(headerscope_app PROPERTIES
    OUTPUT_NAME HeaderScope
    MACOSX_BUNDLE_BUNDLE_NAME HeaderScope
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.deforc.headerscope
)
