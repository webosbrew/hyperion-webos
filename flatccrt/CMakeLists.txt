add_library(flatccrt STATIC IMPORTED GLOBAL)
set_target_properties(flatccrt PROPERTIES 
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libflatccrt.a
)
set_target_properties(flatccrt PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)
