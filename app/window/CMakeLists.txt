add_library(headerscope_window STATIC language.cpp language.hpp main_window.cpp main_window.hpp graph_placeholder.cpp graph_placeholder.hpp theme.hpp)
target_include_directories(headerscope_window PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(headerscope_window PUBLIC HeaderScope::scanner HeaderScope::graph_view
    Qt6::Widgets Qt6::Concurrent)
target_compile_definitions(headerscope_window PRIVATE
    HEADERSCOPE_HAS_SCANNER=$<BOOL:${HEADERSCOPE_BUILD_SCANNER}>)
if(MSVC)
    target_compile_options(headerscope_window PRIVATE /utf-8)
endif()

qt_add_executable(headerscope_app WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(headerscope_app PRIVATE
    headerscope_window
)

if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(headerscope_window_tests tests.cpp)
    target_link_libraries(headerscope_window_tests PRIVATE headerscope_window HeaderScope::demo_fixture Qt6::Test)
    add_test(NAME window COMMAND headerscope_window_tests -platform offscreen)
endif()
set_target_properties(headerscope_app PROPERTIES
    OUTPUT_NAME HeaderScope
    MACOSX_BUNDLE_BUNDLE_NAME HeaderScope
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.deforc.headerscope
)

if(APPLE)
    set(app_icon icons/HeaderScope.icns)
    set_source_files_properties("${app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(headerscope_app PRIVATE "${app_icon}")
    set_target_properties(headerscope_app PROPERTIES MACOSX_BUNDLE_ICON_FILE HeaderScope.icns)
endif()

install(TARGETS headerscope_app BUNDLE DESTINATION . RUNTIME DESTINATION .)
if(HEADERSCOPE_BUILD_SCANNER)
    if(APPLE)
        set(clang_resources HeaderScope.app/Contents/Resources/clang)
    else()
        set(clang_resources clang)
    endif()
    install(DIRECTORY "${HEADERSCOPE_CLANG_RESOURCE_INCLUDE}/"
        DESTINATION "${clang_resources}/include")
endif()
