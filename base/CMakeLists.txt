set(TARGET_NAME base)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui")
file(GLOB IMGUI_SOURCES 
    ${IMGUI_DIR}/*.c
    ${IMGUI_DIR}/*.cpp
    ${IMGUI_DIR}/*.h
    ${IMGUI_DIR}/*.hpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
set_target_properties(imgui PROPERTIES FOLDER "externals")


set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data/shaders")
file(GLOB_RECURSE SHADERS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
    ${SHADER_DIR}/*.tesc
    ${SHADER_DIR}/*.tese
    ${SHADER_DIR}/*.geom
)

GroupSources("base")
GroupSources("data/shaders")
foreach(SHADER ${SHADERS})
    compile_spirv_shader(${SHADER})
    list(APPEND COMPILED_SHADERS ${COMPILE_SPIRV_SHADER_RETURN})
    source_group("compiled" FILES ${COMPILE_SPIRV_SHADER_RETURN})
endforeach()
add_custom_target(shaders SOURCES ${SHADERS} ${COMPILED_SHADERS})

file(GLOB_RECURSE COMMON_SOURCE *.cpp *.h *.hpp)
add_library(${TARGET_NAME} STATIC ${COMMON_SOURCE})
target_link_libraries(${TARGET_NAME} imgui)

target_glfw3()
target_glm()
target_gli()
target_vulkan()
target_assimp()

if (ANDROID)
    add_dependencies(${TARGET_NAME} app-glue)
    target_link_libraries(${TARGET_NAME} app-glue android log m)
endif()