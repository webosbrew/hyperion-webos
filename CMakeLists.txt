cmake_minimum_required(VERSION 3.6)
project(hyperion-webos VERSION 1.0 LANGUAGES C)

add_subdirectory(tv-native-apis)
add_subdirectory(flatccrt)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)

add_executable(hyperion-webos
    src/capture.c
    src/hyperion_client.c
 )
target_include_directories(hyperion-webos PRIVATE fbs)
target_link_libraries(hyperion-webos vt flatccrt PkgConfig::SDL2 PkgConfig::GLES)