# 
#  Created by Bradley Austin Davis on 2016/02/16
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
# 
macro(TARGET_GLFW3)
    if (WIN32)
        add_dependency_external_projects(glfw3)
        add_dependencies(${TARGET_NAME} glfw3)
    elseif(NOT ANDROID)
        pkg_check_modules(GLFW3 REQUIRED glfw3>=3.2)
        link_directories(${GLFW3_LIBRARY_DIRS})
    endif()
    target_include_directories(${TARGET_NAME} PUBLIC ${GLFW3_INCLUDE_DIR})
    target_link_libraries(${TARGET_NAME} ${GLFW3_LIBRARY})
endmacro()