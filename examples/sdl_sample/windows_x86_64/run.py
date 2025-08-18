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
    install_sdl3,
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
    local_webrtc_build_dir: Optional[str],
    local_webrtc_build_args: List[str],
    local_sora_cpp_sdk_dir: Optional[str],
    local_sora_cpp_sdk_args: List[str],
):
    with cd(BASE_DIR):
        version = read_version_file("DEPS")

        # WebRTC
        if local_webrtc_build_dir is None:
            install_webrtc_args = {
                "version": version["WEBRTC_BUILD_VERSION"],
                "version_file": os.path.join(install_dir, "webrtc.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
                "platform": "windows_x86_64",
            }
            install_webrtc(**install_webrtc_args)
        else:
            build_webrtc_args = {
                "platform": "windows_x86_64",
                "local_webrtc_build_dir": local_webrtc_build_dir,
                "local_webrtc_build_args": local_webrtc_build_args,
                "debug": debug,
            }
            build_webrtc(**build_webrtc_args)

        # Sora C++ SDK, Boost
        if local_sora_cpp_sdk_dir is None:
            install_sora_and_deps(
                version["SORA_CPP_SDK_VERSION"],
                version["BOOST_VERSION"],
                "windows_x86_64",
                source_dir,
                install_dir,
            )
        else:
            build_sora(
                "windows_x86_64",
                local_sora_cpp_sdk_dir,
                local_sora_cpp_sdk_args,
                debug,
                local_webrtc_build_dir,
            )

        # CMake
        install_cmake_args = {
            "version": version["CMAKE_VERSION"],
            "version_file": os.path.join(install_dir, "cmake.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "windows-x86_64",
            "ext": "zip",
        }
        install_cmake(**install_cmake_args)

        add_path(os.path.join(install_dir, "cmake", "bin"))

        # SDL3
        install_sdl3_args = {
            "version": version["SDL3_VERSION"],
            "version_file": os.path.join(install_dir, "sdl3.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "debug": debug,
            "platform": "windows",
            "cmake_args": [],
        }
        install_sdl3(**install_sdl3_args)

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
    parser.add_argument("--install-dir")
    add_webrtc_build_arguments(parser)
    add_sora_arguments(parser)

    args = parser.parse_args()

    configuration_dir = "debug" if args.debug else "release"
    platform = "windows_x86_64"
    source_dir = os.path.join(BASE_DIR, "_source", platform, configuration_dir)
    build_dir = os.path.join(BASE_DIR, "_build", platform, configuration_dir)
    install_dir = os.path.join(BASE_DIR, "_install", platform, configuration_dir)
    if args.install_dir is not None:
        install_dir = args.install_dir
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(
        source_dir,
        build_dir,
        install_dir,
        args.debug,
        args.local_webrtc_build_dir,
        args.local_webrtc_build_args,
        args.local_sora_cpp_sdk_dir,
        args.local_sora_cpp_sdk_args,
    )

    configuration = "Debug" if args.debug else "Release"

    sample_build_dir = os.path.join(build_dir, "sdl_sample")
    mkdir_p(sample_build_dir)
    with cd(sample_build_dir):
        webrtc_info = get_webrtc_info(
            "windows_x86_64", args.local_webrtc_build_dir, install_dir, args.debug
        )
        sora_info = get_sora_info(platform, args.local_sora_cpp_sdk_dir, install_dir, args.debug)

        cmake_args = []
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        cmake_args.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(sora_info.boost_install_dir)}")
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}")
        cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}")
        cmake_args.append(f"-DSORA_DIR={cmake_path(sora_info.sora_install_dir)}")
        cmake_args.append(f"-DCLI11_DIR={cmake_path(os.path.join(install_dir, 'cli11'))}")
        cmake_args.append(f"-DSDL3_DIR={cmake_path(os.path.join(install_dir, 'sdl3'))}")
        cmd(["cmake", os.path.join(PROJECT_DIR)] + cmake_args)
        cmd(
            ["cmake", "--build", ".", f"-j{multiprocessing.cpu_count()}", "--config", configuration]
        )


if __name__ == "__main__":
    main()
