# 
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
# 

macro(GroupSources baseDir sources)
   foreach(child ${sources})
       file(RELATIVE_PATH relative ${baseDir} ${child})
       get_filename_component(curdir ${relative} DIRECTORY)
       string(REPLACE "/" "\\" groupname ${curdir})
       source_group(${groupname} FILES ${child})
   endforeach()
endmacro()
