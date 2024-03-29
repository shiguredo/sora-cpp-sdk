import argparse
import multiprocessing
import os
import sys
from typing import List, Optional

PROJECT_DIR = os.path.abspath(os.path.dirname(__file__))
BASE_DIR = os.path.join(PROJECT_DIR, "..", "..")
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
    get_sora_info,
    get_webrtc_info,
    install_cli11,
    install_cmake,
    install_llvm,
    install_sdl2,
    install_sora_and_deps,
    install_webrtc,
    mkdir_p,
    read_version_file,
)


def install_deps(
    source_dir,
    build_dir,
    install_dir,
    debug,
    webrtc_build_dir: Optional[str],
    webrtc_build_args: List[str],
    sora_dir: Optional[str],
    sora_args: List[str],
):
    with cd(BASE_DIR):
        version = read_version_file("VERSION")

        # WebRTC
        if webrtc_build_dir is None:
            install_webrtc_args = {
                "version": version["WEBRTC_BUILD_VERSION"],
                "version_file": os.path.join(install_dir, "webrtc.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
                "platform": "ubuntu-20.04_x86_64",
            }
            install_webrtc(**install_webrtc_args)
        else:
            build_webrtc_args = {
                "platform": "ubuntu-20.04_x86_64",
                "webrtc_build_dir": webrtc_build_dir,
                "webrtc_build_args": webrtc_build_args,
                "debug": debug,
            }
            build_webrtc(**build_webrtc_args)

        webrtc_info = get_webrtc_info("ubuntu-20.04_x86_64", webrtc_build_dir, install_dir, debug)

        if webrtc_build_dir is None:
            webrtc_version = read_version_file(webrtc_info.version_file)

            # LLVM
            tools_url = webrtc_version["WEBRTC_SRC_TOOLS_URL"]
            tools_commit = webrtc_version["WEBRTC_SRC_TOOLS_COMMIT"]
            libcxx_url = webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_URL"]
            libcxx_commit = webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT"]
            buildtools_url = webrtc_version["WEBRTC_SRC_BUILDTOOLS_URL"]
            buildtools_commit = webrtc_version["WEBRTC_SRC_BUILDTOOLS_COMMIT"]
            install_llvm_args = {
                "version": f"{tools_url}.{tools_commit}."
                f"{libcxx_url}.{libcxx_commit}."
                f"{buildtools_url}.{buildtools_commit}",
                "version_file": os.path.join(install_dir, "llvm.version"),
                "install_dir": install_dir,
                "tools_url": tools_url,
                "tools_commit": tools_commit,
                "libcxx_url": libcxx_url,
                "libcxx_commit": libcxx_commit,
                "buildtools_url": buildtools_url,
                "buildtools_commit": buildtools_commit,
            }
            install_llvm(**install_llvm_args)

        # Sora C++ SDK, Boost
        if sora_dir is None:
            install_sora_and_deps("ubuntu-20.04_x86_64", source_dir, install_dir)
        else:
            build_sora("ubuntu-20.04_x86_64", sora_dir, sora_args, debug, webrtc_build_dir)

        # CMake
        install_cmake_args = {
            "version": version["CMAKE_VERSION"],
            "version_file": os.path.join(install_dir, "cmake.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "linux-x86_64",
            "ext": "tar.gz",
        }
        install_cmake(**install_cmake_args)
        add_path(os.path.join(install_dir, "cmake", "bin"))

        # SDL2
        install_sdl2_args = {
            "version": version["SDL2_VERSION"],
            "version_file": os.path.join(install_dir, "sdl2.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "debug": debug,
            "platform": "linux",
            "cmake_args": [
                f"-DCMAKE_C_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang')}",
                f"-DCMAKE_CXX_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang++')}",
            ],
        }
        install_sdl2(**install_sdl2_args)

        # CLI11
        install_cli11_args = {
            "version": version["CLI11_VERSION"],
            "version_file": os.path.join(install_dir, "cli11.version"),
            "install_dir": install_dir,
        }
        install_cli11(**install_cli11_args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--debug", action="store_true")
    add_webrtc_build_arguments(parser)
    add_sora_arguments(parser)

    args = parser.parse_args()

    configuration_dir = "debug" if args.debug else "release"
    platform = "ubuntu-20.04_x86_64"
    source_dir = os.path.join(BASE_DIR, "_source", platform, configuration_dir)
    build_dir = os.path.join(BASE_DIR, "_build", platform, configuration_dir)
    install_dir = os.path.join(BASE_DIR, "_install", platform, configuration_dir)
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(
        source_dir,
        build_dir,
        install_dir,
        args.debug,
        args.webrtc_build_dir,
        args.webrtc_build_args,
        args.sora_dir,
        args.sora_args,
    )

    configuration = "Debug" if args.debug else "Release"

    sample_build_dir = os.path.join(build_dir, "sdl_sample")
    mkdir_p(sample_build_dir)
    with cd(sample_build_dir):
        webrtc_info = get_webrtc_info(
            "ubuntu-20.04_x86_64", args.webrtc_build_dir, install_dir, args.debug
        )
        sora_info = get_sora_info(platform, args.sora_dir, install_dir, args.debug)

        cmake_args = []
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(sora_info.boost_install_dir)}")
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}")
        cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}")
        cmake_args.append(f"-DSORA_DIR={cmake_path(sora_info.sora_install_dir)}")
        cmake_args.append(f"-DCLI11_DIR={cmake_path(os.path.join(install_dir, 'cli11'))}")
        cmake_args.append(f"-DSDL2_DIR={cmake_path(os.path.join(install_dir, 'sdl2'))}")

        # クロスコンパイルの設定。
        # 本来は toolchain ファイルに書く内容
        cmake_args += [
            f"-DCMAKE_C_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang')}",
            f"-DCMAKE_CXX_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang++')}",
            f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}",
        ]

        cmd(["cmake", os.path.join(PROJECT_DIR)] + cmake_args)
        cmd(
            ["cmake", "--build", ".", f"-j{multiprocessing.cpu_count()}", "--config", configuration]
        )


if __name__ == "__main__":
    main()
