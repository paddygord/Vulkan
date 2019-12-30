set(TARGET_NAME base)
add_library(${TARGET_NAME} SHARED)
set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "common")
add_dependencies(${TARGET_NAME} shaders)

# Common functionality
target_include_directories(${TARGET_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Source files
file(GLOB_RECURSE COMMON_SOURCE *.cpp *.hpp)
target_sources(${TARGET_NAME} PRIVATE ${COMMON_SOURCE})

# Dependencies
target_link_libraries(base PUBLIC Vulkan::Vulkan)
target_link_libraries(base PUBLIC glm)
target_link_libraries(base PUBLIC glfw)
target_link_libraries(base PUBLIC ${ASSIMP_LIBRARIES})
target_link_libraries(base PUBLIC imgui::imgui)

# Preprocessor defines for GLM and Vulkan
target_compile_definitions(base PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_FORCE_CTOR_INIT)
if (ANDROID)
    target_compile_definitions(base PUBLIC VK_USE_PLATFORM_ANDROID_KHR)
elseif (WIN32)
    target_compile_definitions(base PUBLIC VK_USE_PLATFORM_WIN32_KHR)
else()
    target_compile_definitions(base PUBLIC VK_USE_PLATFORM_XCB_KHR)
    find_package(XCB REQUIRED)
    target_link_libraries(base PUBLIC XCB::XCB)
endif()

find_package(Threads REQUIRED)
target_link_libraries(base PUBLIC ${CMAKE_THREAD_LIBS_INIT})



# target_basisu()
# target_glfw3()
# target_glm()
# target_gli()
# target_vulkan()
# target_assimp()
# target_imgui()
# target_glad()

# if (ANDROID)
#     add_dependencies(${TARGET_NAME} app-glue)
#     target_link_libraries(${TARGET_NAME} PRIVATE app-glue android log m)
# endif()