cmake_minimum_required(VERSION 3.6)
project(hyperion-webos VERSION 1.0 LANGUAGES C CXX)

set(_cflags_list "")
list(APPEND _cflags_list
    -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon
)
list(JOIN _cflags_list " " COMMON_FLAGS)

add_subdirectory(libyuv)
add_subdirectory(tv-native-apis)
add_subdirectory(fbs)

add_compile_options(${_cflags_list} -Wall -Wextra -Wpedantic -Werror -DSECURITY_COMPATIBILITY)

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

add_subdirectory(unicapture)

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
    src/log.c
    src/utils.c
    src/hyperion_client.c
    src/json_rpc_client.c
)
set_target_properties(hyperion-webos PROPERTIES
    BUILD_RPATH_USE_ORIGIN ON
    INSTALL_RPATH "$ORIGIN:$ORIGIN/lib"
    BUILD_RPATH "$ORIGIN:$ORIGIN/lib"
    # TODO: use above options after upgrading to CMake 3.11
    # LINK_FLAGS "-Wl,-rpath,'$ORIGIN:$ORIGIN/lib' -Wl,-z,origin"
)
target_include_directories(hyperion-webos PRIVATE ${CMAKE_BINARY_DIR})
target_link_libraries(hyperion-webos fbs flatccrt pthread dl unicapture rt ${GTHREAD2_LDFLAGS} ${PBNJSON_LDFLAGS} ${LS2_LDFLAGS} ${GLIB2_LDFLAGS} ${PMLOG_LDFLAGS})
add_dependencies(hyperion-webos version)
set_property(TARGET hyperion-webos PROPERTY ENABLE_EXPORTS 1)
