# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "MainApp\\CMakeFiles\\MediaCountdowns_autogen.dir\\AutogenUsed.txt"
  "MainApp\\CMakeFiles\\MediaCountdowns_autogen.dir\\ParseCache.txt"
  "MainApp\\MediaCountdowns_autogen"
  "TrayApp\\CMakeFiles\\MediaCountdownsNotifier_autogen.dir\\AutogenUsed.txt"
  "TrayApp\\CMakeFiles\\MediaCountdownsNotifier_autogen.dir\\ParseCache.txt"
  "TrayApp\\MediaCountdownsNotifier_autogen"
  )
endif()
