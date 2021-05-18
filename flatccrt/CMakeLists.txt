add_library(flatccrt STATIC IMPORTED GLOBAL)
set_target_properties(flatccrt PROPERTIES 
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libflatccrt.a
)
target_include_directories(flatccrt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)