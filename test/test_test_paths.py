# Copyright 2026 Open Source Robotics Foundation, Inc.
# Licensed under the Apache License, Version 2.0.

import os
from pathlib import Path

import pytest

import ros_test_paths


def test_env_override_wins(monkeypatch):
    monkeypatch.setenv('ROS_TEST_SOURCE_DIR', '/some/where')
    assert ros_test_paths.source_dir() == Path('/some/where')
    assert ros_test_paths.test_file('a', 'b.yaml') == Path('/some/where/test/a/b.yaml')
    assert ros_test_paths.source_file('x.txt') == Path('/some/where/x.txt')


def test_finds_this_package():
    # Works from the source tree (CMake) and from the runfiles (Bazel).
    root = ros_test_paths.source_dir()
    assert (root / 'package.xml').is_file()
    assert ros_test_paths.test_file('test_test_paths.py').is_file()


def test_unconfigured_explains_itself(tmp_path, monkeypatch):
    monkeypatch.delenv('ROS_TEST_SOURCE_DIR', raising=False)
    monkeypatch.delenv('RUNFILES_DIR', raising=False)
    monkeypatch.delenv('RUNFILES_MANIFEST_FILE', raising=False)
    monkeypatch.delenv('TEST_SRCDIR', raising=False)
    with pytest.raises(RuntimeError, match='cannot locate'):
        ros_test_paths.source_dir(anchor=str(tmp_path / 'nowhere' / 'x.py'))


def test_temp_dir_exists():
    assert ros_test_paths.temp_dir().is_dir()


def test_env_is_clean():
    assert 'ROS_TEST_SOURCE_DIR' not in os.environ


def test_copy_to_temp_directory_is_writable_and_cleaned_up(tmp_path):
    src = tmp_path / 'src_dir'
    (src / 'sub').mkdir(parents=True)
    (src / 'a.txt').write_text('hello')
    (src / 'sub' / 'b.txt').write_text('world')
    (src / 'a.txt').chmod(0o444)  # read-only, like a Bazel runfile
    link = tmp_path / 'link.txt'
    link.symlink_to(src / 'sub' / 'b.txt')
    (src / 'sub' / 'link_to_b.txt').symlink_to(src / 'sub' / 'b.txt')

    copy = ros_test_paths.copy_to_temp(src)
    root = copy.path
    assert root.name == 'src_dir'
    assert (root / 'sub' / 'link_to_b.txt').is_file()
    assert not (root / 'sub' / 'link_to_b.txt').is_symlink()  # resolved to a real file
    with open(root / 'a.txt', 'a') as f:  # would fail on a read-only copy
        f.write('!')
    (root / 'a.txt-wal').write_text('')  # creating files next to the data works
    assert (src / 'a.txt').read_text() == 'hello'  # source untouched
    copy.cleanup()
    assert not root.exists()
    (src / 'a.txt').chmod(0o644)


def test_copy_to_temp_single_file_context_manager(tmp_path):
    src = tmp_path / 'one.txt'
    src.write_text('x')
    with ros_test_paths.copy_to_temp(src) as path:
        assert path.name == 'one.txt'
        assert path.read_text() == 'x'
        kept = path
    assert not kept.exists()


def test_copy_of_package_test_dir_is_writable():
    # Under Bazel the source is a tree of read-only symlinks (runfiles); the copy must be real,
    # writable files. Under CMake it is the source tree itself.
    with ros_test_paths.copy_to_temp(ros_test_paths.source_dir() / 'test') as path:
        target = path / 'test_test_paths.py'
        assert target.is_file() and not target.is_symlink()
        with open(target, 'a') as f:
            f.write('\n# writable\n')
        (path / 'sidecar-wal').write_text('')
