cmake_minimum_required(VERSION 3.20)

project(hyprtab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hyprtab MODULE
    src/main.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    pixman-1
)

target_link_libraries(hyprtab PRIVATE PkgConfig::deps)

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
