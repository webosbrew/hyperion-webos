include(ExternalProject)

ExternalProject_Add(
  libyuv
  URL https://chromium.googlesource.com/libyuv/libyuv/+archive/fdc71956bdade0012b0628b5011b567c06c72308.tar.gz
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_FLAGS=${COMMON_FLAGS}
    -DCMAKE_CXX_FLAGS=${COMMON_FLAGS}
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

add_library(yuv STATIC IMPORTED GLOBAL)

set_target_properties(yuv PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES      ${CMAKE_CURRENT_SOURCE_DIR}/include
    IMPORTED_LOCATION                  ${CMAKE_CURRENT_SOURCE_DIR}/lib/libyuv.a
)

add_dependencies(yuv libyuv)
