cmake_minimum_required(VERSION 3.21)

# windeployqt selects the plugins. Resolve their DLLs and the application's
# dependencies together so the archive also includes non-Qt DLLs such as LLVM.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM windows+pe)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${runtime_bin}/llvm-objdump.exe")
file(GLOB_RECURSE plugins "${stage}/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${stage}/HeaderScope.exe"
    MODULES ${plugins}
    DIRECTORIES "${runtime_bin}"
    RESOLVED_DEPENDENCIES_VAR dlls
    UNRESOLVED_DEPENDENCIES_VAR missing_dlls
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/"
)
if(missing_dlls)
    message(FATAL_ERROR "Cannot package HeaderScope; missing DLLs: ${missing_dlls}")
endif()
file(COPY ${dlls} DESTINATION "${stage}")
file(WRITE "${stage}/qt.conf" "[Paths]\nPlugins=.\n")
