// Copyright 2026 Open Source Robotics Foundation, Inc.
// Licensed under the Apache License, Version 2.0.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "ros_test_paths/test_paths.hpp"

namespace
{
void set_env(const char * k, const char * v)
{
#ifdef _WIN32
  _putenv_s(k, v);
#else
  if (v[0] == '\0') {unsetenv(k);} else {setenv(k, v, 1);}
#endif
}
}  // namespace

TEST(TestPaths, env_override_wins)
{
  set_env("ROS_TEST_SOURCE_DIR", "/some/where");
  EXPECT_EQ(ros_test_paths::source_dir(), std::filesystem::path("/some/where"));
  EXPECT_EQ(ros_test_paths::test_file("a", "b.yaml"), std::filesystem::path("/some/where/test/a/b.yaml"));
  EXPECT_EQ(ros_test_paths::source_file("x.txt"), std::filesystem::path("/some/where/x.txt"));
  set_env("ROS_TEST_SOURCE_DIR", "");
}

TEST(TestPaths, copy_to_c_buffer)
{
  set_env("ROS_TEST_SOURCE_DIR", "/abc");
  char buf[16];
  ASSERT_TRUE(ros_test_paths::copy_source_dir(buf, sizeof(buf)));
  EXPECT_STREQ(buf, "/abc");
  char tiny[3];
  EXPECT_FALSE(ros_test_paths::copy_source_dir(tiny, sizeof(tiny)));
  set_env("ROS_TEST_SOURCE_DIR", "");
}

TEST(TestPaths, temp_dir_exists)
{
  EXPECT_TRUE(std::filesystem::exists(ros_test_paths::temp_dir()));
}

TEST(TestPaths, throws_when_unconfigured)
{
  set_env("ROS_TEST_SOURCE_DIR", "");
  // Only meaningful when neither the define nor Bazel runfiles can answer.
#ifndef ROS_TEST_PATHS_HAVE_BAZEL_RUNFILES
  if (std::string(ROS_TEST_PROJECT_SOURCE_DIR).empty()) {
    EXPECT_THROW(ros_test_paths::source_dir(), std::runtime_error);
    char buf[64];
    EXPECT_FALSE(ros_test_paths::copy_source_dir(buf, sizeof(buf)));
  }
#endif
}

TEST(TestPaths, copy_to_temp_file_and_directory_are_writable_and_cleaned_up)
{
  namespace fs = std::filesystem;
  const fs::path src = fs::temp_directory_path() / "ros_test_paths_src_dir";
  fs::remove_all(src);
  fs::create_directories(src / "sub");
  {std::ofstream(src / "a.txt") << "hello";}
  {std::ofstream(src / "sub" / "b.txt") << "world";}
  fs::permissions(
    src / "a.txt", fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
    fs::perm_options::remove);  // read-only, like a Bazel runfile
  fs::path root;
  {
    auto copy = ros_test_paths::copy_to_temp(src);
    root = copy.path();
    EXPECT_EQ(root.filename(), "ros_test_paths_src_dir");
    ASSERT_TRUE(fs::is_regular_file(root / "a.txt"));
    ASSERT_TRUE(fs::is_regular_file(root / "sub" / "b.txt"));
    std::ofstream out(root / "a.txt", std::ios::app);  // would fail on a read-only copy
    EXPECT_TRUE(out.good());
    out << "!";
    out.close();
    std::ofstream sidecar(root / "a.txt-wal");  // creating files next to the data works
    EXPECT_TRUE(sidecar.good());
    // The source is untouched.
    std::ifstream in(src / "a.txt");
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "hello");
  }
  EXPECT_FALSE(fs::exists(root));  // removed with the TempCopy
  fs::permissions(src / "a.txt", fs::perms::owner_write, fs::perm_options::add);
  fs::remove_all(src);
}

TEST(TestPaths, copy_to_temp_single_file)
{
  namespace fs = std::filesystem;
  const fs::path src = fs::temp_directory_path() / "ros_test_paths_single.txt";
  {std::ofstream(src) << "x";}
  auto copy = ros_test_paths::copy_to_temp(src);
  EXPECT_EQ(copy.path().filename(), "ros_test_paths_single.txt");
  EXPECT_TRUE(fs::is_regular_file(copy.path()));
  fs::remove(src);
}

TEST(TestPaths, copy_of_package_test_dir_is_writable)
{
  namespace fs = std::filesystem;
  auto copy = ros_test_paths::copy_to_temp(ros_test_paths::source_dir() / "test");
  const fs::path target = copy.path() / "test_test_paths.cpp";
  ASSERT_TRUE(fs::is_regular_file(target));
  EXPECT_FALSE(fs::is_symlink(target));
  std::ofstream out(target, std::ios::app);
  EXPECT_TRUE(out.good());
  std::ofstream sidecar(copy.path() / "sidecar-wal");
  EXPECT_TRUE(sidecar.good());
}
