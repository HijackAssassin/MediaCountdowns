# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "CMakeFiles\\MediaCountdowns_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\MediaCountdowns_autogen.dir\\ParseCache.txt"
  "MediaCountdowns_autogen"
  )
endif()
