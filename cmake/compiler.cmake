set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

if (WIN32)
  if (NOT "${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    message( FATAL_ERROR "Only 64 bit builds supported." )
  endif()

  # sets path for Microsoft SDKs
  # if you get build error about missing 'glu32' this path is likely wrong
  if (MSVC_VERSION GREATER_EQUAL 1910) # VS 2017
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /openmp")
  endif()
else ()
endif()

