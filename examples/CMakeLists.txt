macro(ADD_EXAMPLE_BINARY) 
    if (ANDROID)
        add_library(${TARGET_NAME} SHARED ${EXAMPLE_SOURCES})
        add_dependencies(${TARGET_NAME} app-glue)
        target_link_libraries(${TARGET_NAME} app-glue)
    elseif(APPLE)
        add_executable(${TARGET_NAME} ${EXAMPLE_SOURCES})
        target_link_libraries(${TARGET_NAME} "-framework OpenGL -framework Metal -framework MetalKit -framework AppKit -framework QuartzCore")
    else()
        add_executable(${TARGET_NAME} ${EXAMPLE_SOURCES})
    endif()
endmacro()

macro(BUILD_EXAMPLES)
    foreach(_FOLDER_NAME ${ARGN})
        if (ANDROID OR (NOT WIN32))
            if ("${_FOLDER_NAME}" STREQUAL "vr_oculus")
                continue()
            endif()
            if ("${_FOLDER_NAME}" STREQUAL "vr_openvr")
                continue()
            endif()
        endif()

        file(GLOB EXAMPLE_SOURCES
            ${_FOLDER_NAME}/*.cpp
            ${_FOLDER_NAME}/*.c
            ${_FOLDER_NAME}/*.h
        )

        if (APPLE) 
            file(GLOB EXAMPLE_SOURCES_OSX
                ${_FOLDER_NAME}/*.mm
                ${_FOLDER_NAME}/*.m
            )
            list(APPEND EXAMPLE_SOURCES ${EXAMPLE_SOURCE_OSX})
        endif()
        set(TARGET_NAME ${_FOLDER_NAME})
        add_example_binary()
        add_dependencies(${TARGET_NAME} base)
        target_include_directories(${TARGET_NAME} PUBLIC base)
        target_link_libraries(${TARGET_NAME} base)
        target_link_libraries(${TARGET_NAME} ${CMAKE_THREAD_LIBS_INIT})
        set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "examples")
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

if (TARGET vr_oculus)
    set(TARGET_NAME vr_oculus)
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "examples/vr")
    target_oculus()
endif()

if (TARGET vr_openvr)
    set(TARGET_NAME vr_openvr)
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "examples/vr")
    target_openvr()
endif()
