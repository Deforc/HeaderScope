add_library(headerscope_demo_fixture STATIC fixture.cpp)
add_library(HeaderScope::demo_fixture ALIAS headerscope_demo_fixture)
target_include_directories(headerscope_demo_fixture PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(headerscope_demo_fixture PUBLIC HeaderScope::contract)
