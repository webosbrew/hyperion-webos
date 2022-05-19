cmake_minimum_required(VERSION 3.6)
project(hyperion-webos VERSION 1.0 LANGUAGES C CXX)

add_subdirectory(libyuv)
add_subdirectory(tv-native-apis)
add_subdirectory(flatccrt)

find_package(PkgConfig REQUIRED)

# ---
# find required packages
include(FindPkgConfig)

pkg_check_modules(GTHREAD2 REQUIRED gthread-2.0)
include_directories(${GTHREAD2_INCLUDE_DIRS})

pkg_check_modules(PBNJSON REQUIRED pbnjson_c)
include_directories(${PBNJSON_INCLUDE_DIRS})

# -- check for glib 2.0
pkg_check_modules(GLIB2 REQUIRED glib-2.0)
include_directories(${GLIB2_INCLUDE_DIRS})

pkg_check_modules(LS2 REQUIRED luna-service2)
include_directories(${LS2_INCLUDE_DIRS})

pkg_check_modules(PMLOG REQUIRED PmLogLib)
include_directories(${PMLOG_INCLUDE_DIRS})

pkg_check_modules(EGL egl REQUIRED)
pkg_check_modules(GLESv2 glesv2 REQUIRED)

find_package(Git)
add_custom_target(version
  ${CMAKE_COMMAND} -D SRC=${CMAKE_SOURCE_DIR}/src/version.h.in
                   -D DST=${CMAKE_BINARY_DIR}/version.h
                   -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
                   -P ${CMAKE_SOURCE_DIR}/GenerateVersionHeader.cmake
)

add_executable(hyperion-webos
    src/main.c
    src/settings.c
    src/service.c
    src/unicapture.c
    src/converter.c
    src/log.c
    src/utils.c
    src/hyperion_client.c
)
set_target_properties(hyperion-webos PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    BUILD_RPATH "$ORIGIN:$ORIGIN/lib"
    # TODO: use above options after upgrading to CMake 3.11
    # LINK_FLAGS "-Wl,-rpath,'$ORIGIN:$ORIGIN/lib' -Wl,-z,origin"
)
target_include_directories(hyperion-webos PRIVATE fbs ${CMAKE_BINARY_DIR})
target_link_libraries(hyperion-webos flatccrt pthread dl yuv rt ${GTHREAD2_LDFLAGS} ${PBNJSON_LDFLAGS} ${LS2_LDFLAGS} ${GLIB2_LDFLAGS} ${PMLOG_LDFLAGS})
add_dependencies(hyperion-webos version)
target_compile_options(hyperion-webos PRIVATE -Wextra -Wpedantic -Werror)
set_property(TARGET hyperion-webos PROPERTY ENABLE_EXPORTS 1)

add_executable(unicapture-test
    src/unicapture-test.c
    src/unicapture.c
    src/converter.c
    src/log.c
    src/utils.c
)
set_target_properties(unicapture-test PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    BUILD_RPATH "$ORIGIN:$ORIGIN/lib"
)
target_link_libraries(unicapture-test pthread dl yuv rt ${PMLOG_LDFLAGS})
set_property(TARGET unicapture-test PROPERTY ENABLE_EXPORTS 1)

# "Unified" v2 Backends
add_library(gm_backend SHARED
    src/backends/libgm.c
)
target_include_directories(gm_backend PRIVATE src src/backends)
target_link_libraries(gm_backend gm)
target_compile_options(gm_backend PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(dile_vt_backend SHARED
    src/backends/libdile_vt.c
)
target_include_directories(dile_vt_backend PRIVATE src src/backends)
target_link_libraries(dile_vt_backend dile_vt)
target_compile_options(dile_vt_backend PRIVATE -Wall -Wextra -Wpedantic -Werror)

# libdile_vt.so seems to be missing DT_NEEDED for libPmLogLib.so.3 - let's just
# add it over to our library. For some reason adding PmLogLib loaded via
# pkgconfig to target_link_libraries doesn't work...
set_target_properties(dile_vt_backend PROPERTIES LINK_FLAGS "-lPmLogLib")

add_library(halgal_backend SHARED
    src/backends/libhalgal.c
)
target_include_directories(halgal_backend PRIVATE src src/backends)
target_link_libraries(halgal_backend halgal)
target_compile_options(halgal_backend PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(vtcapture_backend SHARED
    src/backends/libvtcapture.cpp
)
target_include_directories(vtcapture_backend PRIVATE src src/backends)
target_link_libraries(vtcapture_backend vtcapture)
target_compile_options(vtcapture_backend PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Legacy backends
add_library(vt_legacy_backend SHARED
    src/backends/libvt_legacy.c
)
target_compile_definitions(vt_legacy_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(vt_legacy_backend PRIVATE src src/backends)
target_link_libraries(vt_legacy_backend vt ${GLESv2_LIBRARIES} ${EGL_LIBRARIES})

add_library(vtcapture_legacy_backend SHARED
    src/backends/libvtcapture_legacy.c
)
target_compile_definitions(vtcapture_legacy_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(vtcapture_legacy_backend PRIVATE src src/backends)
target_link_libraries(vtcapture_legacy_backend vtcapture halgal yuv)

add_library(dile_vt_legacy_backend SHARED
    src/backends/libdile_vt_legacy.c
)
target_compile_definitions(dile_vt_legacy_backend PRIVATE CAPTURE_BACKEND)
target_include_directories(dile_vt_legacy_backend PRIVATE src src/backends)
target_link_libraries(dile_vt_legacy_backend dile_vt yuv)

# libdile_vt.so seems to be missing DT_NEEDED for libPmLogLib.so.3 - let's just
# add it over to our library. For some reason adding PmLogLib loaded via
# pkgconfig to target_link_libraries doesn't work...
set_target_properties(dile_vt_legacy_backend PROPERTIES LINK_FLAGS "-lPmLogLib")
