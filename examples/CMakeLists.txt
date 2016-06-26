macro(BUILD_EXAMPLES)
    foreach(_FOLDER_NAME ${ARGN})
        message("Folder ${_FOLDER_NAME}")
        file(GLOB EXAMPLES ${_FOLDER_NAME}/*.cpp)
        foreach(EXAMPLE ${EXAMPLES})
            get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
            set(TARGET ${EXAMPLE_NAME}) 
            # Find any shaders
            string(REGEX REPLACE "^.._" "" EXAMPLE_BASE_NAME ${EXAMPLE_NAME})
            set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../data/shaders/${EXAMPLE_BASE_NAME}")
            file(GLOB SHADERS 
                ${SHADER_DIR}/*.vert 
                ${SHADER_DIR}/*.frag 
                ${SHADER_DIR}/*.comp 
                ${SHADER_DIR}/*.tesc 
                ${SHADER_DIR}/*.tese
                ${SHADER_DIR}/*.geom
            )
            source_group("Shaders" FILES ${SHADERS})
            add_executable(${EXAMPLE_NAME} ${EXAMPLE} ${SHADERS})
            set_target_properties(${EXAMPLE_NAME} PROPERTIES FOLDER "examples/${_FOLDER_NAME}")
            foreach(SHADER ${SHADERS})
                compile_spirv_shader(${SHADER})
            endforeach()
            add_dependencies(${EXAMPLE_NAME} base)
            target_link_libraries(${EXAMPLE_NAME} ${EXAMPLE_LIBS})
            if (NOT WIN32)
                target_link_libraries(${EXAMPLE_NAME} Threads::Threads)
            endif()
        endforeach()
    endforeach()
endmacro()


MACRO(SUBDIRLIST result curdir)
  FILE(GLOB children RELATIVE ${curdir} ${curdir}/*)
  SET(dirlist "")
  FOREACH(child ${children})
    IF(IS_DIRECTORY ${curdir}/${child})
        LIST(APPEND dirlist ${child})
    ENDIF()
  ENDFOREACH()
  SET(${result} ${dirlist})
ENDMACRO()

subdirlist(SUBDIRS ${CMAKE_CURRENT_SOURCE_DIR})
build_examples(${SUBDIRS})

set(TARGET_NAME glinterop)
target_opengl()
target_glew()

set(TARGET_NAME stereo)
target_opengl()
target_glew()

set(TARGET_NAME vr_oculus)
target_opengl()
target_glew()
target_oculus()

set(TARGET_NAME vr_openvr)
target_opengl()
target_glew()
target_openvr()

