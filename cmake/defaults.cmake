# setup for find modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/")

if (CMAKE_BUILD_TYPE)
  string(TOUPPER ${CMAKE_BUILD_TYPE} UPPER_CMAKE_BUILD_TYPE)
else ()
  set(UPPER_CMAKE_BUILD_TYPE DEBUG)
endif ()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

set(MY_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${MY_CMAKE_DIR}/modules/")
set(MACRO_DIR "${MY_CMAKE_DIR}/macros")
set(EXTERNAL_PROJECT_DIR "${MY_CMAKE_DIR}/externals")

file(GLOB MY_CUSTOM_MACROS "cmake/macros/*.cmake")
foreach(CUSTOM_MACRO ${MY_CUSTOM_MACROS})
  include(${CUSTOM_MACRO})
endforeach()

set(EXTERNAL_PROJECT_PREFIX "project")
set_property(DIRECTORY PROPERTY EP_PREFIX ${EXTERNAL_PROJECT_PREFIX})
setup_externals_binary_dir()

set(CMAKE_CXX_STANDARD 14)

add_definitions(-DNOMINMAX)
add_definitions(-D_USE_MATH_DEFINES)
add_definitions(-DGLM_FORCE_RADIANS)
add_definitions(-DGLM_FORCE_DEPTH_ZERO_TO_ONE)


