cmake_minimum_required(VERSION 3.6)
project(hyperion-webos VERSION 1.0 LANGUAGES C)

add_subdirectory(tv-native-apis)
add_subdirectory(flatccrt)

find_package(PkgConfig REQUIRED)

pkg_check_modules(EGL egl REQUIRED)
pkg_check_modules(GLESv2 glesv2 REQUIRED)

add_executable(hyperion-webos
    src/main.c
    src/hyperion_client.c
 )
target_include_directories(hyperion-webos PRIVATE fbs)
target_link_libraries(hyperion-webos flatccrt pthread)

# Backends
add_library(libvt_backend SHARED
    src/backends/libvt.c
)

target_compile_definitions(libvt_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(libvt_backend PRIVATE src src/backends)
target_link_libraries(libvt_backend vt ${GLESv2_LIBRARIES} ${EGL_LIBRARIES})

add_library(libvtcapture_backend SHARED
    src/backends/libvtcapture.c
)

target_compile_definitions(libvtcapture_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(libvtcapture_backend PRIVATE src src/backends)