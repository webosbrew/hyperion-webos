include(ExternalProject)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    SET(FLATCC_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/native/bin/flatcc_d)
else ()
    SET(FLATCC_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/native/bin/flatcc)
endif ()

ExternalProject_Add(flatcc-native
        URL https://github.com/dvidelabs/flatcc/archive/v0.6.1.tar.gz
        URL_HASH SHA512=46ba5ca75facc7d3360dba797d24ae7bfe539a854a48831e1c7b96528cf9594d8bea22b267678fd7c6d742b6636d9e52930987119b4c6b2e38d4abe89b990cae
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/native
        -DFLATCC_TEST=OFF
        -DFLATCC_INSTALL=ON
        BUILD_BYPRODUCTS ${FLATCC_EXECUTABLE}
)

SET(FLATCC_EXECUTABLE ${FLATCC_EXECUTABLE})
add_custom_command(
        OUTPUT hyperion_reply_builder.h hyperion_reply_reader.h flatbuffers_common_builder.h flatbuffers_common_reader.h
        COMMAND ${FLATCC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/hyperion_reply.fbs -w --common_reader --common_builder
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hyperion_reply.fbs
)

add_custom_command(
        OUTPUT hyperion_request_builder.h hyperion_request_reader.h
        COMMAND ${FLATCC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/hyperion_request.fbs -w
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hyperion_request.fbs
)

add_custom_target(generate-fbs DEPENDS
        flatbuffers_common_builder.h
        flatbuffers_common_reader.h
        hyperion_reply_builder.h
        hyperion_reply_reader.h
        hyperion_request_builder.h
        hyperion_request_reader.h)
add_dependencies(generate-fbs flatcc-native)

add_library(fbs INTERFACE)
target_include_directories(fbs SYSTEM INTERFACE ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(fbs generate-fbs)


if (CMAKE_BUILD_TYPE MATCHES "Debug")
    SET(FLATCC_LIB ${CMAKE_CURRENT_BINARY_DIR}/lib/libflatccrt_d.a)
else ()
    SET(FLATCC_LIB ${CMAKE_CURRENT_BINARY_DIR}/lib/libflatccrt.a)
endif ()

ExternalProject_Add(flatcc-target
        URL https://github.com/dvidelabs/flatcc/archive/v0.6.1.tar.gz
        URL_HASH SHA512=46ba5ca75facc7d3360dba797d24ae7bfe539a854a48831e1c7b96528cf9594d8bea22b267678fd7c6d742b6636d9e52930987119b4c6b2e38d4abe89b990cae
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_FLAGS=${COMMON_FLAGS}
        -DCMAKE_CXX_FLAGS=${COMMON_FLAGS}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DBUILD_SHARED_LIBS=OFF
        -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}
        -DFLATCC_TEST=OFF
        -DFLATCC_RTONLY=ON
        -DFLATCC_INSTALL=ON
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        BUILD_BYPRODUCTS ${FLATCC_LIB}
)

add_library(flatccrt IMPORTED STATIC GLOBAL)
add_dependencies(flatccrt flatcc-target)
set(FLATCCRT_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/include)
file(MAKE_DIRECTORY ${FLATCCRT_INCLUDE_DIRS})
set_target_properties(flatccrt PROPERTIES
        IMPORTED_LOCATION ${FLATCC_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${FLATCCRT_INCLUDE_DIRS}
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${FLATCCRT_INCLUDE_DIRS}
)
