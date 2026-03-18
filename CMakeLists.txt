cmake_minimum_required(VERSION 3.20)

project(hyprtab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hyprtab MODULE
    src/main.cpp
)

target_include_directories(hyprtab PRIVATE
    /usr/include/pixman-1
    /usr/include/libdrm
    /usr/include/hyprland/protocols
)

target_compile_options(hyprtab PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

set_target_properties(hyprtab PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hyprtab"
)

install(TARGETS hyprtab
    LIBRARY DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/hyprland/plugins"
)
