set(EXTERNAL_NAME glslang)
string(TOUPPER ${EXTERNAL_NAME} EXTERNAL_NAME_UPPER)

include(ExternalProject)

ExternalProject_Add(
  ${EXTERNAL_NAME}
  URL https://github.com/jherico/glslang/archive/debug_postfix.zip
  URL_MD5 21ff82538ba493c88a377bf501717fc0
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR>
  BINARY_DIR ${EXTERNAL_PROJECT_PREFIX}/build
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
)

# Hide this external target (for ide users)
set_target_properties(${EXTERNAL_NAME} PROPERTIES FOLDER "externals")

# includes
ExternalProject_Get_Property(${EXTERNAL_NAME} SOURCE_DIR)
list(APPEND INCLUDE_DIRS ${SOURCE_DIR}/glslang/Include)
list(APPEND INCLUDE_DIRS ${SOURCE_DIR}/SPIRV)
set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIR ${INCLUDE_DIRS} CACHE PATH "List of glslang include directories")
set(${EXTERNAL_NAME_UPPER}_INCLUDE_DIRS ${${EXTERNAL_NAME_UPPER}_INCLUDE_DIR} CACHE PATH "List of glslang include directories")


ExternalProject_Get_Property(${EXTERNAL_NAME} INSTALL_DIR)
set(LIB_DIR ${INSTALL_DIR}/lib)
list(APPEND LIB_NAMES glslang HLSL OGLCompiler OSDependent SPIRV)
include(SelectLibraryConfigurations)


foreach(BASE_LIB ${LIB_NAMES})
    string(TOUPPER ${BASE_LIB} BASE_LIB_UPPER)
    list(APPEND ${EXTERNAL_NAME_UPPER}_LIBRARY_RELEASE "${LIB_DIR}/${BASE_LIB}.lib")
    list(APPEND ${EXTERNAL_NAME_UPPER}_LIBRARY_DEBUG "${LIB_DIR}/${BASE_LIB}d.lib")
endforeach()

select_library_configurations(${EXTERNAL_NAME_UPPER})

set(${EXTERNAL_NAME_UPPER}_LIBRARY ${${EXTERNAL_NAME_UPPER}_LIBRARY} CACHE FILEPATH "Location of glslang libraries")
set(${EXTERNAL_NAME_UPPER}_LIBRARIES ${${EXTERNAL_NAME_UPPER}_LIBRARIES} CACHE FILEPATH "Location of glslang libraries")
