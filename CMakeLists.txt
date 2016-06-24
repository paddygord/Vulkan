cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
cmake_policy(VERSION 2.8)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
include("cmake/defaults.cmake")
set(NAME vulkanExamples)

project(${NAME})

add_custom_target(SetupRelease ALL ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin)
set_target_properties(SetupRelease PROPERTIES FOLDER "CMakeTargets")
add_custom_target(SetupDebug ALL ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin_debug)
set_target_properties(SetupDebug PROPERTIES FOLDER "CMakeTargets")

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

    include_directories(${GLFW3_INCLUDE_DIR})
    link_libraries(${GLFW3_LIBRARY})

    add_dependency_external_projects(assimp)
    list(APPEND EXTERNALS assimp)
    add_dependency_external_projects(zlib)
    include_directories(${ZLIB_INCLUDE_DIRS})
    link_libraries(${ZLIB_LIBRARIES})
    include_directories(${ASSIMP_INCLUDE_DIRS})
    link_libraries(${ASSIMP_LIBRARIES})
else()

    pkg_check_modules(GLFW3 REQUIRED glfw3>=3.2)
    link_libraries(${GLFW3_LIBRARIES})
    include_directories(${GLFW3_INCLUDEDIR})

    find_package(assimp)
    link_libraries(${ASSIMP_LIBRARIES})
    include_directories(${ASSIMP_INCLUDEDIR})
    get_cmake_property(VARNAMES VARIABLES)

endif()

add_dependency_external_projects(glm)
list(APPEND EXTERNALS glm)
include_directories(${GLM_INCLUDE_DIRS})

add_dependency_external_projects(vkcpp)
list(APPEND EXTERNALS vkcpp)
include_directories(${VKCPP_INCLUDE_DIRS})

add_dependency_external_projects(gli)
list(APPEND EXTERNALS gli)
include_directories(${GLI_INCLUDE_DIRS})

add_dependency_external_projects(glslang)
list(APPEND EXTERNALS glslang)
include_directories(${GLSLANG_INCLUDE_DIRS})
link_libraries(${GLSLANG_LIBRARIES})

add_subdirectory(base)
link_libraries(base)
include_directories(base)

foreach(EXTERNAL ${EXTERNALS})
    add_dependencies(base ${EXTERNAL})
endforeach()

if (NOT WIN32)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    link_libraries(${CMAKE_THREAD_LIBS_INIT})
endif()


macro(BUILD_EXAMPLES)
    foreach(_FOLDER_NAME ${ARGN})
        message("Folder ${_FOLDER_NAME}")
        file(GLOB EXAMPLES examples/${_FOLDER_NAME}/*.cpp)
        foreach(EXAMPLE ${EXAMPLES})
            get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
            set(TARGET ${EXAMPLE_NAME}) 
            # Find any shaders
            string(REGEX REPLACE "^.._" "" EXAMPLE_BASE_NAME ${EXAMPLE_NAME})
            set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/${EXAMPLE_BASE_NAME}")
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
            set_target_properties(${EXAMPLE_NAME} PROPERTIES FOLDER "examples/${_FOLDER_NAME}")
            foreach(SHADER ${SHADERS})
                compile_spirv_shader(${SHADER})
            endforeach()
            add_dependencies(${EXAMPLE_NAME} base)
            target_link_libraries(${EXAMPLE_NAME} ${EXAMPLE_LIBS})
            if (NOT WIN32)
                target_link_libraries(${EXAMPLE_NAME} Threads::Threads)
            endif()
        endforeach()
    endforeach()
endmacro()


MACRO(SUBDIRLIST result curdir)
  FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
  SET(dirlist "")
  FOREACH(child ${children})
    IF(IS_DIRECTORY ${curdir}/${child})
        LIST(APPEND dirlist ${child})
    ENDIF()
  ENDFOREACH()
  SET(${result} ${dirlist})
ENDMACRO()

subdirlist(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR}/examples)
build_examples(${SUBDIRS})

#set(TARGET_NAME glinterop)
#add_dependency_external_projects(glew)
#target_include_directories(${TARGET_NAME} PUBLIC ${GLEW_INCLUDE_DIRS})
#target_compile_definitions(${TARGET_NAME} PUBLIC -DGLEW_STATIC)
#target_link_libraries(${TARGET_NAME} ${GLEW_LIBRARY})
#add_dependencies(${TARGET_NAME} glew) 
#target_opengl()
