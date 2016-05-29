file(GLOB COMMON_SOURCE *.cpp)
add_library(base ${COMMON_SOURCE})
set_target_properties(base PROPERTIES FOLDER "common")