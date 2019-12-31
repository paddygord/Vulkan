cmake_minimum_required(VERSION 3.16 FATAL_ERROR)


include("cmake/defaults.cmake")
include(${CMAKE_SOURCE_DIR}/cmake/ezvcpkg/ezvcpkg.cmake)
ezvcpkg_fetch(
    PACKAGES assimp basisu imgui glad glfw3 gli glm vulkan vulkanmemoryallocator
    UPDATE_TOOLCHAIN
)

set(NAME VulkanCppExamples)
project(${NAME})

find_package(Vulkan REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

if (NOT "${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    message( FATAL_ERROR "Only 64 bit builds supported." )
endif()

include("cmake/compiler.cmake")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin_debug")
set(CMAKE_DEBUG_POSTFIX "d")

add_custom_target(SetupRelease ALL ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin)
set_target_properties(SetupRelease PROPERTIES FOLDER "CMakeTargets")
add_custom_target(SetupDebug ALL ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin_debug)
set_target_properties(SetupDebug PROPERTIES FOLDER "CMakeTargets")


add_subdirectory(data/shaders)

add_subdirectory(base)

add_subdirectory(examples)
