set(SHADER_DIR "${PROJECT_SOURCE_DIR}/data/shaders")
file(GLOB_RECURSE SHADERS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
    ${SHADER_DIR}/*.tesc
    ${SHADER_DIR}/*.tese
    ${SHADER_DIR}/*.geom
)
# GroupSources("data/shaders")
# GroupSources("base")
# foreach(SHADER ${SHADERS})
#     compile_spirv_shader(${SHADER})
#     list(APPEND COMPILED_SHADERS ${COMPILE_SPIRV_SHADER_RETURN})
#     source_group("compiled" FILES ${COMPILE_SPIRV_SHADER_RETURN})
# endforeach()
add_custom_target(shaders SOURCES ${SHADERS} ${COMPILED_SHADERS})
set_target_properties(shaders PROPERTIES FOLDER "common")
