set(TARGET_NAME common)

set(SHADER_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data/shaders")
set(SHADER_DIR "${SHADER_ROOT_DIR}/${EXAMPLE_BASE_NAME}")
file(GLOB_RECURSE SHADERS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
    ${SHADER_DIR}/*.tesc
    ${SHADER_DIR}/*.tese
    ${SHADER_DIR}/*.geom
)

source_group("Shaders" FILES ${SHADERS})
foreach(SHADER ${SHADERS})
    compile_spirv_shader(${SHADER})
    list(APPEND COMPILED_SHADERS ${COMPILE_SPIRV_SHADER_RETURN})
endforeach()
source_group("Shaders\\Compiled" FILES ${COMPILED_SHADERS})

file(GLOB COMMON_SOURCE *.cpp *.h *.hpp)
add_library(${TARGET_NAME} STATIC ${COMMON_SOURCE} ${COMPILED_SHADERS})
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "examples")

target_vulkan()
target_glfw3()
target_glm()
target_gli()
target_assimp()

if (ANDROID)
    add_dependencies(${TARGET_NAME} app-glue)
    target_link_libraries(${TARGET_NAME} app-glue android log m)
endif()