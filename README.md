# ros_test_paths

One API for tests to find their resources, working the same under CMake (colcon) and Bazel.

- **C++** (`ros_test_paths/test_paths.hpp`, header-only): `source_dir()`, `source_file()`, `test_file()`, `copy_source_dir()`, `temp_dir()`, `scratch_dir()`, `copy_to_temp()` / `TempCopy`.
- **Python** (`ros_test_paths`): the same, plus `executable(name)` for sibling built executables.

Package root lookup order: env `ROS_TEST_SOURCE_DIR`; compile define `ROS_TEST_PROJECT_SOURCE_DIR` (CMake, via `ros_test_paths_enable()`); Bazel runfiles (sentinel `package.xml`); else an error naming the reason. Python walks up to the nearest `package.xml` instead of using a define.

## CMake

```cmake
find_package(ros_test_paths REQUIRED)  # test_depend
ament_add_gtest(my_test test/my_test.cpp)
target_link_libraries(my_test ros_test_paths::ros_test_paths)
```

`ament_add_gtest` should define `ROS_TEST_PROJECT_SOURCE_DIR` for every test; that is a one-line change to `ament_cmake_gtest` (`patches/ament_cmake_gtest-source-dir-define.patch`, proposed upstream). Until it lands, call `ros_test_paths_enable(my_test)` instead, which adds the define and the include path.

## Bazel

```starlark
load("@ros_test_paths//:defs.bzl", "ros_cc_test", "ros_py_test")
ros_cc_test(name = "my_test", srcs = ["my_test.cpp"])
```

The macros add the runfiles dep (needed for `BAZEL_CURRENT_REPOSITORY`) and `//:package.xml` as data. Export it from the package: `exports_files(["package.xml"])`.

Status: spike. See https://github.com/mjcarroll/ros-project-management/issues/83.
