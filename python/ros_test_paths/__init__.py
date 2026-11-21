# Copyright 2026 Open Source Robotics Foundation, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Test-only helpers to locate a package's test data under CMake and Bazel.

Same contract as the C++ header ``ros_test_paths/test_paths.hpp``. ``source_dir()`` resolves, in order:

1. environment variable ``ROS_TEST_SOURCE_DIR`` (explicit override);
2. Bazel runfiles (``python.runfiles``), for the repository of the calling test;
3. the nearest directory above the calling file that contains ``package.xml``
   (this is the CMake/colcon case: tests run from the source tree).

Otherwise ``RuntimeError`` is raised, saying why.
"""

import atexit
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile
from typing import Optional, Union

__all__ = ['TempCopy', 'copy_to_temp', 'executable', 'scratch_dir', 'source_dir', 'source_file', 'temp_dir',
           'test_file']

_ENV_OVERRIDE = 'ROS_TEST_SOURCE_DIR'


def _caller_file(depth: int) -> Optional[str]:
    try:
        return sys._getframe(depth).f_globals.get('__file__')
    except ValueError:
        return None


def _bazel_source_dir(frames_to_user: int) -> Optional[Path]:
    """Return the package root in the Bazel runfiles, or None if not running under Bazel."""
    if not (os.environ.get('RUNFILES_DIR') or os.environ.get('RUNFILES_MANIFEST_FILE') or
            os.environ.get('TEST_SRCDIR')):
        return None
    try:
        from python.runfiles import Runfiles  # type: ignore[import-not-found]
    except ImportError:
        return None
    runfiles = Runfiles.Create()
    if runfiles is None:
        return None
    # CurrentRepository() maps a calling file to its canonical repository name. Its frame 0 is
    # itself and frame 1 is this function, so the user's frame is frames_to_user + 1.
    repo = runfiles.CurrentRepository(frame=frames_to_user + 1)
    sentinel = runfiles.Rlocation(f'{repo}/package.xml' if repo else '_main/package.xml')
    if sentinel and os.path.exists(sentinel):
        return Path(sentinel).parent
    return None


def _walk_up_to_package_xml(anchor: Optional[str]) -> Optional[Path]:
    if not anchor:
        return None
    for parent in Path(anchor).resolve().parents:
        if (parent / 'package.xml').is_file():
            return parent
    return None


def source_dir(anchor: Optional[str] = None, _depth: int = 0) -> Path:
    """
    Return the root directory of the package under test.

    :param anchor: file used for the source-tree fallback; defaults to the caller's ``__file__``.
    """
    override = os.environ.get(_ENV_OVERRIDE)
    if override:
        return Path(override)
    if anchor is None:
        anchor = _caller_file(2 + _depth)
    # From _bazel_source_dir: 1 = source_dir, 2 = source_dir's caller (+ _depth wrappers).
    found = _bazel_source_dir(2 + _depth)
    if found is None:
        found = _walk_up_to_package_xml(anchor)
    if found is None:
        raise RuntimeError(
            'ros_test_paths: cannot locate the package source directory: not under Bazel '
            '(or package.xml is not in the runfiles; use ros_py_test or add //:package.xml to '
            f'data) and no package.xml above {anchor!r}. Set {_ENV_OVERRIDE} to override.')
    return found


def source_file(*parts: str, anchor: Optional[str] = None) -> Path:
    """Path of a file relative to the package root, e.g. ``source_file('test', 'a.yaml')``."""
    return source_dir(anchor, _depth=1).joinpath(*parts)


def test_file(*parts: str, anchor: Optional[str] = None) -> Path:
    """Path of a file relative to the package's ``test/`` directory."""
    return source_dir(anchor, _depth=1).joinpath('test', *parts)


def executable(name: str, package_path: str = '') -> Path:
    """
    Return the path of a built executable that lives next to the test (a sibling target).

    Order: ``$ROS_TEST_BINARY_DIR``, then the Bazel runfiles (``<package_path>/<name>`` in the
    package's repository), then the current directory, which is where CMake launch tests run
    (``WORKING_DIRECTORY $<TARGET_FILE_DIR:...>``). ``.exe`` is appended on Windows.

    :param name: executable (target) name.
    :param package_path: Bazel package of the target, relative to the repository root.
    """
    fname = name + ('.exe' if os.name == 'nt' else '')
    override = os.environ.get('ROS_TEST_BINARY_DIR')
    if override:
        return Path(override) / fname
    if (os.environ.get('RUNFILES_DIR') or os.environ.get('RUNFILES_MANIFEST_FILE') or
            os.environ.get('TEST_SRCDIR')):
        try:
            from python.runfiles import Runfiles  # type: ignore[import-not-found]
            runfiles = Runfiles.Create()
        except ImportError:
            runfiles = None
        if runfiles is not None:
            repo = runfiles.CurrentRepository(frame=2) or '_main'
            rel = '/'.join(x for x in (repo, package_path, fname) if x)
            found = runfiles.Rlocation(rel)
            if found and os.path.exists(found):
                return Path(found)
    return Path.cwd() / fname


def temp_dir() -> Path:
    """Directory tests may write to: ``$TEST_UNDECLARED_OUTPUTS_DIR`` (Bazel) or the system temp."""
    bazel = os.environ.get('TEST_UNDECLARED_OUTPUTS_DIR')
    return Path(bazel) if bazel else Path(tempfile.gettempdir())


def scratch_dir() -> Path:
    """Scratch directory for copies: ``$TEST_TMPDIR`` (Bazel, cleaned per test) or system temp."""
    bazel = os.environ.get('TEST_TMPDIR')
    return Path(bazel) if bazel else Path(tempfile.gettempdir())


class TempCopy:
    """
    A writable copy of a file or directory, removed on ``cleanup()``, on leaving a ``with``, or at exit.

    Bazel runfiles are read-only symlinks into the source tree, and some tests write next to
    their data (e.g. SQLite creating -shm/-wal files next to a bag). Symlinks are resolved so the
    copy is made of real files, and everything in it is made owner-writable.
    """

    def __init__(self, source: Union[str, 'os.PathLike[str]']) -> None:
        source = Path(source)
        self._root: Optional[Path] = Path(tempfile.mkdtemp(prefix='ros_test_copy_',
                                                           dir=scratch_dir()))
        self.path = self._root / source.name
        try:
            if source.is_dir():
                shutil.copytree(source, self.path, symlinks=False)
            else:
                shutil.copy(source, self.path)
            for entry in [self.path, *self.path.rglob('*')]:
                mode = entry.stat().st_mode
                entry.chmod(mode | stat.S_IRUSR | stat.S_IWUSR | (stat.S_IXUSR if entry.is_dir()
                                                                  else 0))
        except BaseException:
            self.cleanup()
            raise
        atexit.register(self.cleanup)

    def cleanup(self) -> None:
        if self._root is not None:
            shutil.rmtree(self._root, ignore_errors=True)
            self._root = None

    def __fspath__(self) -> str:
        return str(self.path)

    def __enter__(self) -> Path:
        return self.path

    def __exit__(self, *exc: object) -> None:
        self.cleanup()


def copy_to_temp(source: Union[str, 'os.PathLike[str]']) -> TempCopy:
    """Make a writable copy of ``source`` (see :class:`TempCopy`); keep the object alive."""
    return TempCopy(source)
