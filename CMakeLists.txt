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

add_dependency_external_projects(glm)
include_directories(${GLM_INCLUDE_DIRS})

add_dependency_external_projects(vkcpp)
include_directories(${VKCPP_INCLUDE_DIRS})

add_dependency_external_projects(assimp)
include_directories(${ASSIMP_INCLUDE_DIRS})
link_libraries(${ASSIMP_LIBRARIES})

add_dependency_external_projects(gli)
include_directories(${GLI_INCLUDE_DIRS})

add_dependency_external_projects(zlib)
include_directories(${ZLIB_INCLUDE_DIRS})
link_libraries(${ZLIB_LIBRARIES})

add_dependency_external_projects(glslang)
include_directories(${GLSLANG_INCLUDE_DIRS})
link_libraries(${GLSLANG_LIBRARIES})

add_subdirectory(base)
link_libraries(base)
include_directories(base)
add_dependencies(base glm)
add_dependencies(base gli)
add_dependencies(base vkcpp)
add_dependencies(base assimp)
add_dependencies(base zlib)

# Function for building single example
function(buildExample EXAMPLE_NAME)
	file(GLOB SOURCE ${EXAMPLE_NAME}/*.cpp)
	if(WIN32)
		add_executable(${EXAMPLE_NAME} WIN32 ${EXAMPLE_NAME}/${EXAMPLE_NAME}.cpp ${SOURCE})
	else()
		add_executable(${EXAMPLE_NAME} ${EXAMPLE_NAME}/${EXAMPLE_NAME}.cpp ${SOURCE})
	endif()
    target_link_libraries(${EXAMPLE_NAME} ${EXAMPLE_LIBS})
endfunction()

set(EXAMPLES
	bloom
	computeshader
	computeparticles
	deferred
	displacement
	distancefieldfonts
	gears
	geometryshader
	instancing
        indirect
	mesh
	multithreading
	multisampling
	occlusionquery
	offscreen
	parallaxmapping
	particlefire
	pipelines
	pushconstants
	raytracing
	radialblur
	shadowmapping
	shadowmappingomni
	skeletalanimation
	sphericalenvmapping
	tessellation
	textoverlay
	texture
	texturearray
	texturecubemap
	triangle
	vulkanscene
)

foreach(EXAMPLE ${EXAMPLES})
    buildExample(${EXAMPLE})
endforeach(EXAMPLE)
