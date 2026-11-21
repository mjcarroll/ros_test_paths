# Copyright 2026 Open Source Robotics Foundation, Inc.
# Licensed under the Apache License, Version 2.0.

# ros_test_paths_enable(<target> [<target> ...])
#
# Tell the given test targets where the package source tree is, so ros_test_paths::source_dir()
# works under CMake without a WORKING_DIRECTORY or per-package TEST_RESOURCES_DIRECTORY define.
function(ros_test_paths_enable)
  foreach(_target ${ARGN})
    target_include_directories(${_target} PRIVATE
      "$<TARGET_PROPERTY:ros_test_paths::ros_test_paths,INTERFACE_INCLUDE_DIRECTORIES>")
    target_compile_definitions(${_target} PRIVATE
      "ROS_TEST_PROJECT_SOURCE_DIR=\"${PROJECT_SOURCE_DIR}\"")
  endforeach()
endfunction()
