cmake_minimum_required(VERSION 3.20)

project(hyprtab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hyprtab MODULE
    src/main.cpp
)

set(HYPRTAB_INCLUDE_DIRS)

if (EXISTS "/usr/include/pixman-1")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/include/pixman-1")
endif()

if (EXISTS "/usr/local/include/pixman-1")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/local/include/pixman-1")
endif()

if (EXISTS "/usr/include/libdrm")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/include/libdrm")
endif()

if (EXISTS "/usr/local/include/libdrm")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/local/include/libdrm")
endif()

if (EXISTS "/usr/include/hyprland/protocols")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/include/hyprland/protocols")
endif()

if (EXISTS "/usr/include/hyprland/src")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/include/hyprland/src")
endif()

if (EXISTS "/usr/local/include/hyprland/protocols")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/local/include/hyprland/protocols")
endif()

if (EXISTS "/usr/local/include/hyprland/src")
    list(APPEND HYPRTAB_INCLUDE_DIRS "/usr/local/include/hyprland/src")
endif()

target_include_directories(hyprtab PRIVATE ${HYPRTAB_INCLUDE_DIRS})

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
