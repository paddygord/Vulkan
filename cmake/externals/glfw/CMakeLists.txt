include(ExternalProject)
include(SelectLibraryConfigurations)

set(EXTERNAL_NAME glfw)

string(TOUPPER ${EXTERNAL_NAME} EXTERNAL_NAME_UPPER)

ExternalProject_Add(
    ${EXTERNAL_NAME}
    URL https://github.com/glfw/glfw/archive/3.2.zip
    URL_MD5 6028bc8cb169376ac1dad3ab3bd216cf
    CMAKE_ARGS ${PLATFORM_CMAKE_ARGS} -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>  -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
    LOG_DOWNLOAD 1
)
  
ExternalProject_Get_Property(${EXTERNAL_NAME} INSTALL_DIR)
set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIRS ${INSTALL_DIR}/include CACHE TYPE INTERNAL)
set(${EXTERNAL_NAME_UPPER}_LIBRARIES ${INSTALL_DIR}/lib/glfw3.lib CACHE TYPE INTERNAL)

# Hide this external target (for ide users)
set_target_properties(${EXTERNAL_NAME} PROPERTIES FOLDER "externals")


