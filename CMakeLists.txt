cmake_minimum_required(VERSION 3.20)

project(MineFetch VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "MineFetch 目前只支持 Windows（WinHTTP/Win32 API）。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(minefetch
    src/common.cpp
    src/folder_dialog.cpp
    src/http.cpp
    src/installer.cpp
    src/json.cpp
    src/main.cpp
    src/minecraft.cpp
    src/mirror.cpp
    src/remote.cpp
    src/sha1.cpp
)

target_compile_definitions(minefetch PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

target_link_libraries(minefetch PRIVATE winhttp bcrypt ole32 uuid shell32)

if(MSVC)
    set_property(TARGET minefetch PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(minefetch PRIVATE /utf-8 /W4 /permissive- /EHsc)
    target_compile_options(minefetch PRIVATE "$<$<CONFIG:Release>:/O2>")
    target_link_options(minefetch PRIVATE /SUBSYSTEM:CONSOLE)
else()
    target_compile_options(minefetch PRIVATE -Wall -Wextra -Wpedantic
        -finput-charset=UTF-8 -fexec-charset=UTF-8)
    if(MINGW)
        target_link_options(minefetch PRIVATE -municode -static
            -static-libgcc -static-libstdc++)
    endif()
endif()

set_target_properties(minefetch PROPERTIES
    OUTPUT_NAME "minefetch"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

enable_testing()
add_test(NAME minefetch_self_test COMMAND minefetch --self-test)

install(TARGETS minefetch RUNTIME DESTINATION .)
