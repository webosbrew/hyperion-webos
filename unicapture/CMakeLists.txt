add_library(unicapture STATIC
        unicapture.c
        converter.c
)
set(UNICAPTURE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_include_directories(unicapture PUBLIC ${UNICAPTURE_INCLUDE_DIRS})
target_link_libraries(unicapture PUBLIC yuv)

# "Unified" v2 Backends
add_library(gm_backend SHARED
        backends/libgm.c
)
target_include_directories(gm_backend PRIVATE ${UNICAPTURE_INCLUDE_DIRS} backends)
target_link_libraries(gm_backend gm)

add_library(dile_vt_backend SHARED
        backends/libdile_vt.c
)
target_include_directories(dile_vt_backend PRIVATE ${UNICAPTURE_INCLUDE_DIRS} backends)
target_link_libraries(dile_vt_backend dile_vt)

# libdile_vt.so seems to be missing DT_NEEDED for libPmLogLib.so.3 - let's just
# add it over to our library. For some reason adding PmLogLib loaded via
# pkgconfig to target_link_libraries doesn't work...
set_target_properties(dile_vt_backend PROPERTIES LINK_FLAGS "-lPmLogLib")

add_library(halgal_backend SHARED
        backends/libhalgal.c
)
target_include_directories(halgal_backend PRIVATE ${UNICAPTURE_INCLUDE_DIRS} backends)
target_link_libraries(halgal_backend halgal)

add_library(vtcapture_backend SHARED
        backends/libvtcapture.cpp
)
target_include_directories(vtcapture_backend PRIVATE ${UNICAPTURE_INCLUDE_DIRS} backends)
target_link_libraries(vtcapture_backend vtcapture)

add_dependencies(unicapture gm_backend dile_vt_backend halgal_backend vtcapture_backend)
