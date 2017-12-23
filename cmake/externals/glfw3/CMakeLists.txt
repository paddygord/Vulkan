set(EXTERNAL_NAME glfw3)
string(TOUPPER ${EXTERNAL_NAME} EXTERNAL_NAME_UPPER)

include(ExternalProject)
ExternalProject_Add(
    ${EXTERNAL_NAME}
    URL https://codeload.github.com/glfw/glfw/zip/3.2.1
    URL_MD5 cb412cba8daba35d849ddfe78700a210
    CMAKE_ARGS ${PLATFORM_CMAKE_ARGS} -DCMAKE_BUILD_TYPE=RELEASE -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
    LOG_DOWNLOAD 1
)

ExternalProject_Get_Property(${EXTERNAL_NAME} INSTALL_DIR)
set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIR ${INSTALL_DIR}/include CACHE TYPE INTERNAL)
set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIRS ${INSTALL_DIR}/include CACHE TYPE INTERNAL)
#set(${EXTERNAL_NAME_UPPER}_LIBRARY_DEBUG ${INSTALL_DIR}/lib/glfw3dll.lib CACHE FILEPATH "Path to glfw debug library")
set(${EXTERNAL_NAME_UPPER}_LIBRARY_RELEASE ${INSTALL_DIR}/lib/glfw3dll.lib CACHE FILEPATH "Path to glfw release library")
include(SelectLibraryConfigurations)
select_library_configurations(${EXTERNAL_NAME_UPPER})
set(${EXTERNAL_NAME_UPPER}_LIBRARY ${${EXTERNAL_NAME_UPPER}_LIBRARY} CACHE TYPE INTERNAL)
# copy the DLL 
add_custom_command(TARGET ${EXTERNAL_NAME} POST_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy ${INSTALL_DIR}/lib/glfw3.dll ${CMAKE_SOURCE_DIR}/bin)
add_custom_command(TARGET ${EXTERNAL_NAME} POST_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy ${INSTALL_DIR}/lib/glfw3.dll ${CMAKE_SOURCE_DIR}/bin_debug)

# Hide this external target (for ide users)
set_target_properties(${EXTERNAL_NAME} PROPERTIES FOLDER "externals")


