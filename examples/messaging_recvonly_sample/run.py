import argparse
import hashlib
import multiprocessing
import os
import sys
from dataclasses import dataclass
from typing import Any, Callable, Dict, List, Optional, Tuple

PROJECT_DIR = os.path.abspath(os.path.dirname(__file__))
BASE_DIR = os.path.join(PROJECT_DIR, "..")
BUILDBASE_DIR = os.path.join(BASE_DIR, "..")
sys.path.insert(0, BUILDBASE_DIR)


from buildbase import (  # noqa: E402
    add_path,
    add_sora_arguments,
    add_webrtc_build_arguments,
    build_sora,
    build_webrtc,
    cd,
    cmake_path,
    cmd,
    cmdcap,
    get_sora_info,
    get_webrtc_info,
    install_cli11,
    install_cmake,
    install_llvm,
    install_rootfs,
    install_sora_and_deps,
    install_webrtc,
    mkdir_p,
    read_version_file,
)


@dataclass(frozen=True)
class PlatformConfig:
    platform: str
    install_cmake_platform: str
    install_cmake_ext: str
    cmake_bin_components: Tuple[str, ...]
    cmake_toolchain_args: Callable[["BuildContext", Any], List[str]]
    requires_llvm: bool = True
    rootfs_conf: Optional[str] = None
    supports_relwithdebinfo: bool = False


@dataclass
class BuildContext:
    args: argparse.Namespace
    platform: str
    source_dir: str
    build_dir: str
    install_dir: str
    debug: bool


def macos_cmake_toolchain_args(_: BuildContext, webrtc_info: Any) -> List[str]:
    sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
    return [
        "-DCMAKE_SYSTEM_PROCESSOR=arm64",
        "-DCMAKE_OSX_ARCHITECTURES=arm64",
        "-DCMAKE_C_COMPILER_TARGET=aarch64-apple-darwin",
        "-DCMAKE_CXX_COMPILER_TARGET=aarch64-apple-darwin",
        f"-DCMAKE_SYSROOT={sysroot}",
        f"-DCMAKE_C_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang')}",
        f"-DCMAKE_CXX_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang++')}",
        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}",
    ]


def linux_x86_cmake_toolchain_args(_: BuildContext, webrtc_info: Any) -> List[str]:
    return [
        f"-DCMAKE_C_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang')}",
        f"-DCMAKE_CXX_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang++')}",
        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}",
    ]


def linux_rootfs_cmake_toolchain_args(ctx: BuildContext, webrtc_info: Any) -> List[str]:
    sysroot = os.path.join(ctx.install_dir, "rootfs")
    return [
        f"-DCMAKE_C_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang')}",
        f"-DCMAKE_CXX_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang++')}",
        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}",
        "-DCMAKE_SYSTEM_NAME=Linux",
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
        f"-DCMAKE_SYSROOT={sysroot}",
        "-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu",
        "-DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu",
        f"-DCMAKE_FIND_ROOT_PATH={sysroot}",
        "-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER",
        "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH",
        "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH",
        "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH",
    ]


def windows_cmake_toolchain_args(_: BuildContext, __: Any) -> List[str]:
    return []


PLATFORMS: Dict[str, PlatformConfig] = {
    "macos_arm64": PlatformConfig(
        platform="macos_arm64",
        install_cmake_platform="macos-universal",
        install_cmake_ext="tar.gz",
        cmake_bin_components=("cmake", "CMake.app", "Contents", "bin"),
        cmake_toolchain_args=macos_cmake_toolchain_args,
    ),
    "ubuntu-22.04_x86_64": PlatformConfig(
        platform="ubuntu-22.04_x86_64",
        install_cmake_platform="linux-x86_64",
        install_cmake_ext="tar.gz",
        cmake_bin_components=("cmake", "bin"),
        cmake_toolchain_args=linux_x86_cmake_toolchain_args,
    ),
    "ubuntu-24.04_x86_64": PlatformConfig(
        platform="ubuntu-24.04_x86_64",
        install_cmake_platform="linux-x86_64",
        install_cmake_ext="tar.gz",
        cmake_bin_components=("cmake", "bin"),
        cmake_toolchain_args=linux_x86_cmake_toolchain_args,
    ),
    "ubuntu-22.04_armv8": PlatformConfig(
        platform="ubuntu-22.04_armv8",
        install_cmake_platform="linux-x86_64",
        install_cmake_ext="tar.gz",
        cmake_bin_components=("cmake", "bin"),
        cmake_toolchain_args=linux_rootfs_cmake_toolchain_args,
        rootfs_conf="ubuntu-22.04_armv8.conf",
    ),
    "ubuntu-24.04_armv8": PlatformConfig(
        platform="ubuntu-24.04_armv8",
        install_cmake_platform="linux-x86_64",
        install_cmake_ext="tar.gz",
        cmake_bin_components=("cmake", "bin"),
        cmake_toolchain_args=linux_rootfs_cmake_toolchain_args,
        rootfs_conf="ubuntu-24.04_armv8.conf",
    ),
    "windows_x86_64": PlatformConfig(
        platform="windows_x86_64",
        install_cmake_platform="windows-x86_64",
        install_cmake_ext="zip",
        cmake_bin_components=("cmake", "bin"),
        cmake_toolchain_args=windows_cmake_toolchain_args,
        requires_llvm=False,
        supports_relwithdebinfo=True,
    ),
}


def install_deps(config: PlatformConfig, ctx: BuildContext) -> Any:
    with cd(BASE_DIR):
        deps = read_version_file("DEPS")

        if config.rootfs_conf is not None:
            conf = os.path.join(BASE_DIR, "multistrap", config.rootfs_conf)
            with open(conf, "rb") as fp:
                version_md5 = hashlib.md5(fp.read()).hexdigest()
            install_rootfs(
                version=version_md5,
                version_file=os.path.join(ctx.install_dir, "rootfs.version"),
                install_dir=ctx.install_dir,
                conf=conf,
            )

        if ctx.args.local_webrtc_build_dir is None:
            install_webrtc(
                version=deps["WEBRTC_BUILD_VERSION"],
                version_file=os.path.join(ctx.install_dir, "webrtc.version"),
                source_dir=ctx.source_dir,
                install_dir=ctx.install_dir,
                platform=config.platform,
            )
        else:
            build_webrtc(
                platform=config.platform,
                local_webrtc_build_dir=ctx.args.local_webrtc_build_dir,
                local_webrtc_build_args=ctx.args.local_webrtc_build_args,
                debug=ctx.debug,
            )

        webrtc_info = get_webrtc_info(
            config.platform, ctx.args.local_webrtc_build_dir, ctx.install_dir, ctx.debug
        )

        if config.requires_llvm and ctx.args.local_webrtc_build_dir is None:
            webrtc_version = read_version_file(webrtc_info.version_file)
            install_llvm(
                version=".".join(
                    [
                        webrtc_version["WEBRTC_SRC_TOOLS_URL"],
                        webrtc_version["WEBRTC_SRC_TOOLS_COMMIT"],
                        webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_URL"],
                        webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT"],
                        webrtc_version["WEBRTC_SRC_BUILDTOOLS_URL"],
                        webrtc_version["WEBRTC_SRC_BUILDTOOLS_COMMIT"],
                    ]
                ),
                version_file=os.path.join(ctx.install_dir, "llvm.version"),
                install_dir=ctx.install_dir,
                tools_url=webrtc_version["WEBRTC_SRC_TOOLS_URL"],
                tools_commit=webrtc_version["WEBRTC_SRC_TOOLS_COMMIT"],
                libcxx_url=webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_URL"],
                libcxx_commit=webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT"],
                buildtools_url=webrtc_version["WEBRTC_SRC_BUILDTOOLS_URL"],
                buildtools_commit=webrtc_version["WEBRTC_SRC_BUILDTOOLS_COMMIT"],
            )

        if ctx.args.local_sora_cpp_sdk_dir is None:
            install_sora_and_deps(
                deps["SORA_CPP_SDK_VERSION"],
                deps["BOOST_VERSION"],
                config.platform,
                ctx.source_dir,
                ctx.install_dir,
            )
        else:
            build_sora(
                config.platform,
                ctx.args.local_sora_cpp_sdk_dir,
                ctx.args.local_sora_cpp_sdk_args,
                ctx.debug,
                ctx.args.local_webrtc_build_dir,
            )

        install_cmake(
            version=deps["CMAKE_VERSION"],
            version_file=os.path.join(ctx.install_dir, "cmake.version"),
            source_dir=ctx.source_dir,
            install_dir=ctx.install_dir,
            platform=config.install_cmake_platform,
            ext=config.install_cmake_ext,
        )
        add_path(os.path.join(ctx.install_dir, *config.cmake_bin_components))

        install_cli11(
            version=deps["CLI11_VERSION"],
            version_file=os.path.join(ctx.install_dir, "cli11.version"),
            install_dir=ctx.install_dir,
        )

        return webrtc_info


def configure_and_build(config: PlatformConfig, ctx: BuildContext, configuration: str, webrtc_info: Any):
    sample_build_dir = os.path.join(ctx.build_dir, "messaging_recvonly_sample")
    mkdir_p(sample_build_dir)
    with cd(sample_build_dir):
        sora_info = get_sora_info(
            config.platform, ctx.args.local_sora_cpp_sdk_dir, ctx.install_dir, ctx.debug
        )
        cmake_args = [
            f"-DCMAKE_BUILD_TYPE={configuration}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DBOOST_ROOT={cmake_path(sora_info.boost_install_dir)}",
            f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}",
            f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}",
            f"-DSORA_DIR={cmake_path(sora_info.sora_install_dir)}",
            f"-DCLI11_DIR={cmake_path(os.path.join(ctx.install_dir, 'cli11'))}",
        ]
        cmake_args.extend(config.cmake_toolchain_args(ctx, webrtc_info))

        cmd(["cmake", PROJECT_DIR] + cmake_args)
        cmd(
            [
                "cmake",
                "--build",
                ".",
                f"-j{multiprocessing.cpu_count()}",
                "--config",
                configuration,
            ]
        )


def build_command(args: argparse.Namespace) -> None:
    config = PLATFORMS[args.platform]
    if args.relwithdebinfo and not config.supports_relwithdebinfo:
        raise SystemExit("--relwithdebinfo は windows_x86_64 でのみ指定できます。")

    configuration_dir = "debug" if args.debug else "release"
    ctx = BuildContext(
        args=args,
        platform=config.platform,
        source_dir=os.path.join(BASE_DIR, "_source", config.platform, configuration_dir),
        build_dir=os.path.join(BASE_DIR, "_build", config.platform, configuration_dir),
        install_dir=os.path.join(BASE_DIR, "_install", config.platform, configuration_dir)
        if args.install_dir is None
        else args.install_dir,
        debug=args.debug,
    )

    mkdir_p(ctx.source_dir)
    mkdir_p(ctx.build_dir)
    mkdir_p(ctx.install_dir)

    webrtc_info = install_deps(config, ctx)

    if config.supports_relwithdebinfo and args.relwithdebinfo:
        configuration = "RelWithDebInfo"
    elif args.debug:
        configuration = "Debug"
    else:
        configuration = "Release"

    configure_and_build(config, ctx, configuration, webrtc_info)


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build")
    build_parser.add_argument("platform", choices=sorted(PLATFORMS.keys()))
    build_parser.add_argument("--debug", action="store_true")
    build_parser.add_argument("--relwithdebinfo", action="store_true")
    build_parser.add_argument("--install-dir")
    add_webrtc_build_arguments(build_parser)
    add_sora_arguments(build_parser)
    build_parser.set_defaults(func=build_command)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
