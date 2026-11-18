cmake_minimum_required(VERSION 3.20)
project(ef2000_explorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)

add_executable(ef2000-explorer
    src/main.cpp
    src/parser.cpp
    src/render.cpp
    src/ra.cpp
    src/did.cpp
    src/shape3d.cpp
    src/meshview.cpp
    src/modelview.cpp
    src/texture.cpp
    src/gui.cpp
    src/guirender.cpp
    src/ilbm.cpp)

target_link_libraries(ef2000-explorer PRIVATE
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
    PkgConfig::SDL2_IMAGE)

target_compile_options(ef2000-explorer PRIVATE -Wall -Wextra -Wpedantic)
