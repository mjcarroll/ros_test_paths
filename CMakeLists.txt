cmake_minimum_required(VERSION 3.20)
project(ros_test_paths)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)

add_library(ros_test_paths INTERFACE)
add_library(ros_test_paths::ros_test_paths ALIAS ros_test_paths)
target_compile_features(ros_test_paths INTERFACE cxx_std_17)
target_include_directories(ros_test_paths INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})
install(TARGETS ros_test_paths EXPORT export_${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})

ament_python_install_package(${PROJECT_NAME} PACKAGE_DIR python/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_gtest(test_test_paths test/test_test_paths.cpp)
  target_link_libraries(test_test_paths ros_test_paths)
  ament_add_pytest_test(test_test_paths_py test/test_test_paths.py)
endif()

ament_package(CONFIG_EXTRAS cmake/ros_test_paths_enable.cmake)
