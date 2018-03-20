macro(TARGET_LIBPNG)
    set(LIBPNG_DIR "c:/dev/libpng")
    set(LIBPNG_INCLUDE_DIRS "${LIBPNG_DIR}/include")
    set(LIBPNG_LIBRARIES "${LIBPNG_DIR}/lib/libpng16_staticd.lib")
    set(ZLIB_LIBRARIES "${LIBPNG_DIR}/lib/zlibstaticd.lib")
    
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBPNG_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBPNG_LIBRARIES} ${ZLIB_LIBRARIES})
endmacro()
