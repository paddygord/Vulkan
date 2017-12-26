set(TARGET_NAME base)

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data/shaders")
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
GroupSources("data/shaders")
add_custom_target(shaders SOURCES ${SHADERS})

file(GLOB COMMON_SOURCE *.cpp *.h *.hpp)
add_library(${TARGET_NAME} STATIC ${COMMON_SOURCE} ${COMPILED_SHADERS})

target_glfw3()
target_glm()
target_gli()
target_vulkan()
target_assimp()

if (ANDROID)
    add_dependencies(${TARGET_NAME} app-glue)
    target_link_libraries(${TARGET_NAME} app-glue android log m)
endif()