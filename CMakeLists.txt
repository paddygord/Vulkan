cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
cmake_policy(VERSION 2.8)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
include("cmake/defaults.cmake")
set(NAME vulkanExamples)

project(${NAME})

find_package(Vulkan REQUIRED)
link_libraries(${VULKAN_LIBRARY})
include_directories(${VULKAN_INCLUDE_DIR})

if(WIN32)
    add_definitions(-DVK_USE_PLATFORM_WIN32_KHR)
else()
    add_definitions(-DVK_USE_PLATFORM_XCB_KHR)

    find_package(XCB REQUIRED)
    link_libraries(${XCB_LIBRARIES})
endif()

if (WIN32)
    add_dependency_external_projects(glfw3)
    list(APPEND EXTERNALS glfw3)
else()
    find_package(glfw3 3.2 REQUIRED glfw3)
    message("GLFW ${GLFW3_LIBRARIES}")
    message("GLFW ${GLFW3_INCLUDE_DIRS}")
    get_cmake_property(_variableNames VARIABLES)
    foreach (_variableName ${_variableNames})
        string(TOLOWER ${_variableName} VAR_LOWER)
        string(REGEX MATCH glfw MATCHED ${VAR_LOWER})
        if (NOT MATCHED) 
	    continue()
        endif()
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
endif()

include_directories(${GLFW3_INCLUDE_DIR})
link_libraries(${GLFW3_LIBRARY})


find_package(assimp)
if (NOT assimp_FOUND)
    find_package(zlib)
    if (NOT ZLIB_FOUND)
        add_dependency_external_projects(zlib)
        add_dependencies(assimp zlib)
        include_directories(${ZLIB_INCLUDE_DIRS})
        link_libraries(${ZLIB_LIBRARIES})
    endif()
    add_dependency_external_projects(assimp)
    list(APPEND EXTERNALS assimp)
endif()
include_directories(${ASSIMP_INCLUDE_DIRS})
link_libraries(${ASSIMP_LIBRARIES})

add_dependency_external_projects(glm)
include_directories(${GLM_INCLUDE_DIRS})

add_dependency_external_projects(vkcpp)
include_directories(${VKCPP_INCLUDE_DIRS})

add_dependency_external_projects(gli)
include_directories(${GLI_INCLUDE_DIRS})

add_dependency_external_projects(glslang)
include_directories(${GLSLANG_INCLUDE_DIRS})
link_libraries(${GLSLANG_LIBRARIES})

add_subdirectory(base)
link_libraries(base)
include_directories(base)

foreach(EXTERNAL ${EXTERNALS})
    message("external ${EXTERNAL}")
    add_dependencies(base ${EXTERNAL})
endforeach()

add_dependencies(base glm)
add_dependencies(base gli)
add_dependencies(base vkcpp)
add_dependencies(base glslang)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
link_libraries(${CMAKE_THREAD_LIBS_INIT})

file(GLOB EXAMPLES examples/*.cpp)
foreach(EXAMPLE ${EXAMPLES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
    # Find any shaders
    set(SHADER_DIR "data/shaders/${EXAMPLE_NAME}")
    file(GLOB SHADERS 
        ${SHADER_DIR}/*.vert 
        ${SHADER_DIR}/*.frag 
        ${SHADER_DIR}/*.comp 
        ${SHADER_DIR}/*.tesc 
        ${SHADER_DIR}/*.tese
        ${SHADER_DIR}/*.geom
    )
    source_group("Shaders" FILES ${SHADERS})
    add_executable(${EXAMPLE_NAME} ${EXAMPLE} ${SHADERS})
    add_dependencies(${EXAMPLE_NAME} base)
    foreach(EXTERNAL ${EXTERNALS})
        message("external ${EXTERNAL}")
        add_dependencies(${EXAMPLE_NAME} ${EXTERNAL})
    endforeach()
    target_link_libraries(${EXAMPLE_NAME} ${EXAMPLE_LIBS})
    target_link_libraries(${EXAMPLE_NAME} Threads::Threads)
endforeach()

