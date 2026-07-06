include(ExternalProject)

if (CMAKE_BUILD_TYPE MATCHES "Debug")
    SET(FLATCC_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/native/bin/flatcc_d)
else ()
    SET(FLATCC_EXECUTABLE ${CMAKE_CURRENT_BINARY_DIR}/native/bin/flatcc)
endif ()

ExternalProject_Add(flatcc-native
        GIT_REPOSITORY https://github.com/dvidelabs/flatcc.git
        GIT_TAG 29201734bf2d12713a7a1a035d31e5123aac9c93
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
        GIT_REPOSITORY https://github.com/dvidelabs/flatcc.git
        GIT_TAG 29201734bf2d12713a7a1a035d31e5123aac9c93
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
