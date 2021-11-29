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
target_link_libraries(hyperion-webos flatccrt pthread dl)

# Backends
add_library(vt_backend SHARED
    src/backends/libvt.c
)

target_compile_definitions(vt_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(vt_backend PRIVATE src src/backends)
target_link_libraries(vt_backend vt ${GLESv2_LIBRARIES} ${EGL_LIBRARIES})

add_library(vtcapture_backend SHARED
    src/backends/libvtcapture.c
)

target_compile_definitions(vtcapture_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(vtcapture_backend PRIVATE src src/backends)
target_link_libraries(vtcapture_backend vtcapture halgal)

add_library(dile_vt_backend SHARED
    src/backends/libdile_vt.c
)

target_compile_definitions(dile_vt_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(dile_vt_backend PRIVATE src src/backends)
target_link_libraries(dile_vt_backend dile_vt)
