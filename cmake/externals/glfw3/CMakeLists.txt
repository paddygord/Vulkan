set(EXTERNAL_NAME glfw3)
string(TOUPPER ${EXTERNAL_NAME} EXTERNAL_NAME_UPPER)

if (WIN32)
    set(PLATFORM_CMAKE_ARGS -DBUILD_SHARED_LIBS=ON)
    set(PLATFORM_INSTALL_COMMAND "INSTALL_COMMAND \"\"")
else()
    set(PLATFORM_CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>)
    set(PLATFORM_INSTALL_COMMAND "")
endif()

include(ExternalProject)
ExternalProject_Add(
    ${EXTERNAL_NAME}
    URL https://github.com/glfw/glfw/archive/3.2.zip
    URL_MD5 6028bc8cb169376ac1dad3ab3bd216cf
    CMAKE_ARGS ${PLATFORM_CMAKE_ARGS}  -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF
    ${PLATFORM_INSTALL_COMMAND}
    LOG_DOWNLOAD 1
)

if (WIN32)
    ExternalProject_Get_Property(${EXTERNAL_NAME} BINARY_DIR)
    ExternalProject_Get_Property(${EXTERNAL_NAME} SOURCE_DIR)
    set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIR ${SOURCE_DIR}/include CACHE TYPE INTERNAL)
    set(${EXTERNAL_NAME_UPPER}_LIBRARY_DEBUG ${BINARY_DIR}/src/Debug/glfw3dll.lib CACHE FILEPATH "Path to glfw debug library")
    set(${EXTERNAL_NAME_UPPER}_LIBRARY_RELEASE ${BINARY_DIR}/src/Release/glfw3dll.lib CACHE FILEPATH "Path to glfw release library")
    include(SelectLibraryConfigurations)
select_library_configurations(${EXTERNAL_NAME_UPPER})
    set(${EXTERNAL_NAME_UPPER}_LIBRARY ${${EXTERNAL_NAME_UPPER}_LIBRARY} CACHE TYPE INTERNAL)
    # copy the DLL 
    add_custom_command(TARGET ${EXTERNAL_NAME} POST_BUILD 
	COMMAND if 1==$<CONFIG:Release> ${CMAKE_COMMAND} -E copy ${BINARY_DIR}/src/$<CONFIG>/glfw3.dll ${CMAKE_SOURCE_DIR}/bin)
    add_custom_command(TARGET ${EXTERNAL_NAME} POST_BUILD 
	COMMAND if 1==$<CONFIG:Debug> ${CMAKE_COMMAND} -E copy ${BINARY_DIR}/src/$<CONFIG>/glfw3.dll ${CMAKE_SOURCE_DIR}/bin_debug)
else()
    ExternalProject_Get_Property(${EXTERNAL_NAME} INSTALL_DIR)
    set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIR ${INSTALL_DIR}/include CACHE TYPE INTERNAL)
    set(${EXTERNAL_NAME_UPPER}_LIBRARY ${INSTALL_DIR}/lib/libglfw3.a CACHE TYPE INTERNAL)
endif()



# Hide this external target (for ide users)
set_target_properties(${EXTERNAL_NAME} PROPERTIES FOLDER "externals")


