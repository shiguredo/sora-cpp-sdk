from __future__ import annotations

import json
from pathlib import Path

import pytest

from buildbase import install_sysroot
from sysroot_builder import SysrootBuildError, load_sysroot_config, sysroot_config_fingerprint


def write_config(path: Path, *, name: str) -> None:
    # 実際の設定と同じ構造を使い、APT 実行前のラッパ処理だけを検証する。
    keyring_path = path.parent / "keyrings" / "archive-keyring.gpg"
    keyring_path.parent.mkdir(parents=True)
    keyring_path.touch()
    config = {
        "name": name,
        "arch": "arm64",
        "triplet": "aarch64-linux-gnu",
        "packages": ["libc6-dev"],
        "repositories": [
            {
                "url": "https://ports.ubuntu.com/ubuntu-ports",
                "suite": "noble",
                "components": ["main"],
                "signed_by": "keyrings/archive-keyring.gpg",
            }
        ],
    }
    path.write_text(json.dumps(config), encoding="utf-8")


def test_install_sysroot_preserves_legacy_rootfs_for_invalid_config_name(tmp_path: Path) -> None:
    # 設定名を検証する前に旧成果物を削除しないことを確認する。
    install_dir = tmp_path / "install"
    rootfs_dir = install_dir / "rootfs"
    rootfs_dir.mkdir(parents=True)
    marker_path = rootfs_dir / "legacy-marker"
    marker_path.write_text("legacy", encoding="utf-8")
    rootfs_version_path = install_dir / "rootfs.version"
    rootfs_version_path.write_text("legacy", encoding="utf-8")
    config_path = tmp_path / "expected.json"
    write_config(config_path, name="actual")

    with pytest.raises(RuntimeError, match="expected=expected, actual=actual"):
        install_sysroot(str(config_path), str(install_dir))

    assert marker_path.read_text(encoding="utf-8") == "legacy"
    assert rootfs_version_path.read_text(encoding="utf-8") == "legacy"


def test_install_sysroot_preserves_unknown_rootfs_without_legacy_version(tmp_path: Path) -> None:
    # 旧方式の証拠がない既存ディレクトリを黙って削除しないことを確認する。
    install_dir = tmp_path / "install"
    rootfs_dir = install_dir / "rootfs"
    rootfs_dir.mkdir(parents=True)
    marker_path = rootfs_dir / "unknown-marker"
    marker_path.write_text("unknown", encoding="utf-8")
    config_path = tmp_path / "target.json"
    write_config(config_path, name="target")

    with pytest.raises(SysrootBuildError):
        install_sysroot(str(config_path), str(install_dir))

    assert marker_path.read_text(encoding="utf-8") == "unknown"


def test_install_sysroot_preserves_legacy_rootfs_when_build_fails(tmp_path: Path) -> None:
    # 旧成果物の置換準備に失敗しても、利用可能な rootfs と version を残す。
    install_dir = tmp_path / "install"
    rootfs_dir = install_dir / "rootfs"
    rootfs_dir.mkdir(parents=True)
    marker_path = rootfs_dir / "legacy-marker"
    marker_path.write_text("legacy", encoding="utf-8")
    rootfs_version_path = install_dir / "rootfs.version"
    rootfs_version_path.write_text("legacy", encoding="utf-8")
    config_path = tmp_path / "target.json"
    write_config(config_path, name="target")

    # builder が同じ親ディレクトリへ一時領域を作れない状態を実ファイルで再現する。
    install_dir.chmod(0o555)
    try:
        with pytest.raises((OSError, SysrootBuildError)):
            install_sysroot(str(config_path), str(install_dir))
    finally:
        install_dir.chmod(0o755)

    assert marker_path.read_text(encoding="utf-8") == "legacy"
    assert rootfs_version_path.read_text(encoding="utf-8") == "legacy"


def test_install_sysroot_removes_legacy_version_after_reusing_manifest(tmp_path: Path) -> None:
    # 新方式の manifest を再利用できた後にだけ旧 version ファイルを削除する。
    install_dir = tmp_path / "install"
    rootfs_dir = install_dir / "rootfs"
    rootfs_dir.mkdir(parents=True)
    config_path = tmp_path / "target.json"
    write_config(config_path, name="target")
    config = load_sysroot_config(config_path)
    manifest = {
        "format_version": 1,
        "fingerprint": sysroot_config_fingerprint(config),
    }
    (rootfs_dir / ".webrtc-build-sysroot.json").write_text(json.dumps(manifest), encoding="utf-8")
    rootfs_version_path = install_dir / "rootfs.version"
    rootfs_version_path.write_text("legacy", encoding="utf-8")
    other_version_path = install_dir / "boost.version"
    other_version_path.write_text("keep", encoding="utf-8")

    install_sysroot(str(config_path), str(install_dir))

    assert not rootfs_version_path.exists()
    assert other_version_path.read_text(encoding="utf-8") == "keep"
