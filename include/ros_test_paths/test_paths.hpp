// Copyright 2026 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/// Test-only helpers to locate a package's test data under CMake and Bazel.
///
/// Header-only, C++17, standard library only (plus the Bazel runfiles library when
/// building under Bazel). Intended for tests: never link it into a production library.
///
/// source_dir() resolution order:
///   1. environment variable ROS_TEST_SOURCE_DIR       (explicit override)
///   2. compile definition ROS_TEST_PROJECT_SOURCE_DIR (set by CMake, see ros_test_paths_enable())
///   3. Bazel runfiles, for the repository this test was compiled in (BAZEL_CURRENT_REPOSITORY)
/// otherwise std::runtime_error is thrown.

#ifndef ROS_TEST_PATHS__TEST_PATHS_HPP_
#define ROS_TEST_PATHS__TEST_PATHS_HPP_

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <string>
#include <utility>
#include <vector>

#if defined(BAZEL_CURRENT_REPOSITORY) && __has_include("rules_cc/cc/runfiles/runfiles.h")
#include "rules_cc/cc/runfiles/runfiles.h"
#define ROS_TEST_PATHS_HAVE_BAZEL_RUNFILES 1
#endif

#ifndef ROS_TEST_PROJECT_SOURCE_DIR
#define ROS_TEST_PROJECT_SOURCE_DIR ""
#endif

namespace ros_test_paths
{

namespace detail
{

inline std::string getenv_str(const char * name)
{
  const char * v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

#ifdef ROS_TEST_PATHS_HAVE_BAZEL_RUNFILES
/// Package root inside the Bazel runfiles. Works with a runfiles *tree* (Linux/macOS) and with a
/// manifest-only runfiles (Windows): directories are not manifest entries, so the root is derived
/// from a sentinel file every ROS package has, whose mapped path lives in the real package tree.
inline std::string bazel_source_dir(std::string & error)
{
  using rules_cc::cc::runfiles::Runfiles;
  const std::string repo = BAZEL_CURRENT_REPOSITORY;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(repo, &error));
  if (!runfiles) {
    error = "Runfiles::CreateForTest(" + repo + ") failed: " + error;
    return {};
  }
  const std::string sentinel = runfiles->Rlocation(repo + "/package.xml");
  std::error_code ec;
  if (sentinel.empty() || !std::filesystem::exists(sentinel, ec)) {
    error = "package.xml of " + repo + " is not in the runfiles (use ros_cc_test, or add "
      "//:package.xml to the test's data)";
    return {};
  }
  return std::filesystem::path(sentinel).parent_path().string();
}
#endif

}  // namespace detail

/// Root directory of the package under test (the directory containing package.xml and test/).
inline std::filesystem::path source_dir()
{
  std::string dir = detail::getenv_str("ROS_TEST_SOURCE_DIR");
  if (dir.empty()) {
    dir = ROS_TEST_PROJECT_SOURCE_DIR;
  }
#ifdef ROS_TEST_PATHS_HAVE_BAZEL_RUNFILES
  std::string why;
  if (dir.empty()) {
    dir = detail::bazel_source_dir(why);
  }
#else
  std::string why = "built without Bazel runfiles support";
#endif
  if (dir.empty()) {
    throw std::runtime_error(
            "ros_test_paths: cannot locate the package source directory (" + why + "). Set "
            "ROS_TEST_SOURCE_DIR, build with ROS_TEST_PROJECT_SOURCE_DIR (CMake: "
            "ros_test_paths_enable()), or run under Bazel.");
  }
  return std::filesystem::path(dir);
}

/// Path of a file relative to the package root, e.g. source_file("test", "data", "a.yaml").
template<typename ... Args>
std::filesystem::path source_file(const Args & ... args)
{
  std::filesystem::path p = source_dir();
  ((p /= args), ...);
  return p;
}

/// Path of a file relative to the package's test/ directory, e.g. test_file("a.yaml").
template<typename ... Args>
std::filesystem::path test_file(const Args & ... args)
{
  return source_file("test", args ...);
}

/// Copy the package root into a C buffer. Convenience for tests written against C APIs.
/// \return true on success, false if the path does not fit or cannot be determined.
inline bool copy_source_dir(char * buffer, size_t buffer_size)
{
  try {
    const std::string s = source_dir().string();
    if (s.size() + 1 > buffer_size) {
      return false;
    }
    std::memcpy(buffer, s.c_str(), s.size() + 1);
    return true;
  } catch (const std::runtime_error & e) {
    std::fprintf(stderr, "%s\n", e.what());
    return false;
  }
}

/// Array overload: copy_source_dir(char (&buffer)[N]).
template<size_t N>
bool copy_source_dir(char (& buffer)[N])
{
  return copy_source_dir(buffer, N);
}

/// A directory tests may write to: $TEST_UNDECLARED_OUTPUTS_DIR (Bazel), else the system temp dir.
inline std::filesystem::path temp_dir()
{
  const std::string bazel = detail::getenv_str("TEST_UNDECLARED_OUTPUTS_DIR");
  if (!bazel.empty()) {
    return std::filesystem::path(bazel);
  }
  return std::filesystem::temp_directory_path();
}


/// Scratch directory for copies: $TEST_TMPDIR (Bazel, cleaned per test) else the system temp dir.
inline std::filesystem::path scratch_dir()
{
  const std::string bazel = detail::getenv_str("TEST_TMPDIR");
  if (!bazel.empty()) {
    return std::filesystem::path(bazel);
  }
  return std::filesystem::temp_directory_path();
}

/// A writable copy of a file or directory, removed again when the object is destroyed.
///
/// Bazel runfiles are read-only symlinks into the source tree, and some tests write next to their
/// data (e.g. SQLite creating -shm/-wal files next to a bag). Symlinks are resolved, so the copy
/// is made of real files, and every file in it is made owner-writable.
class TempCopy
{
public:
  explicit TempCopy(const std::filesystem::path & source)
  {
    std::random_device rd;
    std::filesystem::path base;
    std::error_code ec;
    for (int attempt = 0; attempt < 100; ++attempt) {
      base = scratch_dir() / ("ros_test_copy_" + std::to_string(rd()) + "_" + std::to_string(rd()));
      if (std::filesystem::create_directory(base, ec)) {
        break;
      }
      base.clear();
    }
    if (base.empty()) {
      throw std::runtime_error("ros_test_paths: cannot create a scratch directory in " +
              scratch_dir().string());
    }
    root_ = base;
    path_ = root_ / source.filename();
    std::filesystem::copy(
      source, path_,
      std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    // copy() preserves permissions; make the copy writable.
    namespace fs = std::filesystem;
    std::vector<fs::path> entries{path_};
    if (fs::is_directory(path_)) {
      for (const auto & e : fs::recursive_directory_iterator(path_)) {
        entries.push_back(e.path());
      }
    }
    for (const auto & e : entries) {
      fs::permissions(e, fs::perms::owner_write | fs::perms::owner_read, fs::perm_options::add);
    }
  }

  TempCopy(const TempCopy &) = delete;
  TempCopy & operator=(const TempCopy &) = delete;
  TempCopy(TempCopy && other) noexcept
  : root_(std::move(other.root_)), path_(std::move(other.path_))
  {
    other.root_.clear();
  }

  ~TempCopy()
  {
    if (!root_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(root_, ec);
    }
  }

  /// Path of the copy (same file or directory name as the source).
  const std::filesystem::path & path() const {return path_;}
  operator const std::filesystem::path &() const {return path_;}

private:
  std::filesystem::path root_;
  std::filesystem::path path_;
};

/// Make a writable copy of `source` (see TempCopy); keep the returned object alive while in use.
inline TempCopy copy_to_temp(const std::filesystem::path & source)
{
  return TempCopy(source);
}

}  // namespace ros_test_paths

#endif  // ROS_TEST_PATHS__TEST_PATHS_HPP_
