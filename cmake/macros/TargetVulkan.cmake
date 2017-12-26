# 
#  Created by Bradley Austin Davis on 2016/02/16
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
# 
macro(TARGET_VULKAN)
    if (ANDROID)
        target_include_directories(${TARGET_NAME} PUBLIC ${VULKAN_SDK}/include)
        target_link_libraries(${TARGET_NAME} vulkan)
    else()
        find_package(Vulkan REQUIRED)
        if (NOT VULKAN_FOUND)
            message(FATAL_ERROR "Unable to locate Vulkan package")
        endif()
        set(Vulkan_LIBRARIES Vulkan::Vulkan)
        target_link_libraries(${TARGET_NAME} ${Vulkan_LIBRARIES})
        target_include_directories(${TARGET_NAME} PUBLIC ${Vulkan_INCLUDE_DIR})
    endif()    
endmacro()