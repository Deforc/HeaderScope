# Replace INTERFACE with a compiled library when the scanner is implemented.
add_library(headerscope_scanner INTERFACE)
add_library(HeaderScope::scanner ALIAS headerscope_scanner)
target_link_libraries(headerscope_scanner INTERFACE HeaderScope::contract)
