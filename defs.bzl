"""Bazel macros for tests that use ros_test_paths."""

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_test")
load("@rosdistro//python:defs.bzl", "py_pytest")

def _add(items, extra):
    # Append only what is missing: Bazel rejects duplicate labels.
    return items + [x for x in extra if x not in items]

def _wire(deps, data):
    # The runfiles library must be a *direct* dep: Bazel only defines
    # BAZEL_CURRENT_REPOSITORY for targets that depend on it directly.
    # //:package.xml marks the package root inside the runfiles.
    return (
        _add(deps, ["@ros_test_paths//:ros_test_paths", "@rules_cc//cc/runfiles"]),
        _add(data, ["//:package.xml"]),
    )

def ros_cc_test(name, deps = [], data = [], **kwargs):
    """cc_test wired up for ros_test_paths."""
    deps, data = _wire(deps, data)
    cc_test(name = name, deps = deps, data = data, **kwargs)

def ros_cc_binary(name, deps = [], data = [], **kwargs):
    """cc_binary wired up for ros_test_paths (e.g. tests run through a wrapper)."""
    deps, data = _wire(deps, data)
    cc_binary(name = name, deps = deps, data = data, **kwargs)

def ros_py_test(name, deps = [], data = [], **kwargs):
    """py_pytest (rosdistro) wired up for ros_test_paths."""
    py_pytest(
        name = name,
        deps = _add(deps, [
            "@ros_test_paths//:ros_test_paths_py",
            "@rules_python//python/runfiles",
        ]),
        data = _add(data, ["//:package.xml"]),
        **kwargs
    )
