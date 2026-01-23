import argparse
import glob
import hashlib
import json
import logging
import multiprocessing
import os
import shutil
import tarfile
import zipfile
from typing import Dict, List, Optional

from buildbase import (
    Platform,
    WebrtcInfo,
    add_path,
    add_webrtc_build_arguments,
    build_and_install_boost,
    build_webrtc,
    cd,
    cmake_path,
    cmd,
    cmdcap,
    enum_all_files,
    fix_clang_version,
    get_clang_version,
    get_macos_osver,
    get_webrtc_info,
    get_webrtc_platform,
    get_windows_osver,
    install_amf,
    install_android_ndk,
    install_android_sdk_cmdline_tools,
    install_blend2d_official,
    install_catch2,
    install_cmake,
    install_cuda_windows,
    install_llvm,
    install_openh264,
    install_rootfs,
    install_vpl,
    install_webrtc,
    mkdir_p,
    read_version_file,
    rm_rf,
)

logging.basicConfig(level=logging.DEBUG)


BASE_DIR = os.path.abspath(os.path.dirname(__file__))


def get_android_abi(platform: Platform) -> str:
    if platform.target.arch == "x86_64":
        return "x86_64"
    return "arm64-v8a"


def get_android_clang_lib_arch(platform: Platform) -> str:
    if platform.target.arch == "x86_64":
        return "x86_64"
    return "aarch64"


def install_llvm_from_webrtc_source(webrtc_source_dir: str, install_dir: str) -> None:
    llvm_dir = os.path.join(install_dir, "llvm")
    rm_rf(llvm_dir)
    mkdir_p(llvm_dir)
    with cd(webrtc_source_dir):
        cmd(
            [
                "python3",
                os.path.join("tools", "clang", "scripts", "update.py"),
                "--output-dir",
                os.path.join(llvm_dir, "clang"),
            ]
        )
    shutil.copytree(
        os.path.join(webrtc_source_dir, "third_party", "libc++", "src"),
        os.path.join(llvm_dir, "libcxx"),
    )
    shutil.copytree(
        os.path.join(webrtc_source_dir, "buildtools"),
        os.path.join(llvm_dir, "buildtools"),
    )
    shutil.copyfile(
        os.path.join(llvm_dir, "buildtools", "third_party", "libc++", "__config_site"),
        os.path.join(llvm_dir, "libcxx", "include", "__config_site"),
    )
    shutil.copyfile(
        os.path.join(llvm_dir, "buildtools", "third_party", "libc++", "__assertion_handler"),
        os.path.join(llvm_dir, "libcxx", "include", "__assertion_handler"),
    )


def read_version(version_path):
    """VERSION ファイルからバージョンを読み込む"""
    with open(version_path, "r") as f:
        return f.read().strip()


def get_common_cmake_args(
    platform: Platform,
    deps: Dict[str, str],
    webrtc_info: WebrtcInfo,
    base_dir: str,
    install_dir: str,
    debug: bool,
):
    args = []

    webrtc_deps = read_version_file(webrtc_info.deps_file)

    if platform.target.os == "windows":
        cxxflags = ["/EHsc", "/D_ITERATOR_DEBUG_LEVEL=0"]
        cxxflags.append("/MTd" if debug else "/MT")
        args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
    if platform.target.os == "macos":
        sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
        target = (
            "x86_64-apple-darwin" if platform.target.arch == "x86_64" else "aarch64-apple-darwin"
        )
        args.append(
            f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
        )
        args.append(
            f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
        )
        args.append(f"-DCMAKE_SYSTEM_PROCESSOR={platform.target.arch}")
        args.append(f"-DCMAKE_OSX_ARCHITECTURES={platform.target.arch}")
        args.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}")
        args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
        args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
        args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
        args.append(f"-DCMAKE_SYSROOT={sysroot}")
        path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
        args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
        cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
        args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
    if platform.target.os == "ubuntu":
        if platform.target.package_name in (
            "ubuntu-22.04_x86_64",
            "ubuntu-24.04_x86_64",
        ):
            apt_install_llvm_version = deps["APT_INSTALL_LLVM_VERSION"]
            args.append(f"-DCMAKE_C_COMPILER=clang-{apt_install_llvm_version}")
            args.append(f"-DCMAKE_CXX_COMPILER=clang++-{apt_install_llvm_version}")
        else:
            sysroot = os.path.join(install_dir, "rootfs")
            args.append(
                f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
            )
            args.append(
                f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
            )
            args.append("-DCMAKE_SYSTEM_NAME=Linux")
            args.append("-DCMAKE_SYSTEM_PROCESSOR=aarch64")
            args.append(f"-DCMAKE_SYSROOT={sysroot}")
            args.append("-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu")
            args.append("-DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu")
            args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
            args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER")
        path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
        args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
        cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
        args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
    if platform.target.os in ("jetson", "raspberry-pi-os"):
        triplet = "aarch64-linux-gnu"
        arch = "aarch64"
        sysroot = os.path.join(install_dir, "rootfs")
        args.append("-DCMAKE_SYSTEM_NAME=Linux")
        args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch}")
        args.append(f"-DCMAKE_C_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang')}")
        args.append(f"-DCMAKE_C_COMPILER_TARGET={triplet}")
        args.append(f"-DCMAKE_CXX_COMPILER={os.path.join(webrtc_info.clang_dir, 'bin', 'clang++')}")
        args.append(f"-DCMAKE_CXX_COMPILER_TARGET={triplet}")
        args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
        args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER")
        args.append("-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH")
        args.append("-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH")
        args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH")
        args.append(f"-DCMAKE_SYSROOT={sysroot}")
        path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
        args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
        cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
        args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
    if platform.target.os == "ios":
        args.append(
            f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
        )
        args.append(
            f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
        )
        args.append("-DCMAKE_SYSTEM_NAME=iOS")
        args.append("-DCMAKE_OSX_ARCHITECTURES=arm64")
        args.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['IOS_DEPLOYMENT_TARGET']}")
        args.append("-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO")
        args.append("-DBLEND2D_NO_JIT=ON")
        path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
        args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
        cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
        args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
    if platform.target.os == "android":
        android_ndk = os.path.join(install_dir, "android-ndk")
        toolchain_file = os.path.join(base_dir, "cmake", "android.toolchain.cmake")
        override_toolchain_file = os.path.join(
            os.path.join(android_ndk, "build", "cmake", "android.toolchain.cmake")
        )
        override_c_compiler = os.path.join(webrtc_info.clang_dir, "bin", "clang")
        override_cxx_compiler = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
        android_clang_dir = os.path.join(
            android_ndk, "toolchains", "llvm", "prebuilt", "linux-x86_64"
        )
        android_abi = get_android_abi(platform)
        clang_lib_arch = get_android_clang_lib_arch(platform)
        sysroot = os.path.join(android_clang_dir, "sysroot")
        android_native_api_level = deps["ANDROID_NATIVE_API_LEVEL"]
        args.append(f"-DANDROID_OVERRIDE_TOOLCHAIN_FILE={cmake_path(override_toolchain_file)}")
        args.append(f"-DANDROID_OVERRIDE_C_COMPILER={cmake_path(override_c_compiler)}")
        args.append(f"-DANDROID_OVERRIDE_CXX_COMPILER={cmake_path(override_cxx_compiler)}")
        args.append(f"-DCMAKE_SYSROOT={cmake_path(sysroot)}")
        args.append(f"-DCMAKE_TOOLCHAIN_FILE={cmake_path(toolchain_file)}")
        args.append(f"-DANDROID_NATIVE_API_LEVEL={android_native_api_level}")
        args.append(f"-DANDROID_PLATFORM={android_native_api_level}")
        args.append(f"-DANDROID_ABI={android_abi}")
        args.append("-DANDROID_STL=none")
        path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
        args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
        args.append("-DANDROID_CPP_FEATURES=exceptions rtti")
        # r23b には ANDROID_CPP_FEATURES=exceptions でも例外が設定されない問題がある
        # https://github.com/android/ndk/issues/1618
        args.append("-DCMAKE_ANDROID_EXCEPTIONS=ON")
        args.append("-DANDROID_NDK=OFF")
        cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
        args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
        clang_version = fix_clang_version(
            android_clang_dir, get_clang_version(os.path.join(android_clang_dir, "bin", "clang++"))
        )
        ldflags = [
            f"-L{cmake_path(os.path.join(android_clang_dir, 'lib', 'clang', clang_version, 'lib', 'linux', clang_lib_arch))}"
        ]
        args.append(f"-DCMAKE_EXE_LINKER_FLAGS={' '.join(ldflags)}")

    return args


def install_deps(
    platform: Platform,
    source_dir: str,
    build_dir: str,
    install_dir: str,
    debug: bool,
    local_webrtc_build_dir: Optional[str],
    local_webrtc_build_args: List[str],
    disable_cuda: bool,
):
    with cd(BASE_DIR):
        deps = read_version_file("DEPS")

        # multistrap を使った sysroot の構築
        if platform.target.package_name in (
            "ubuntu-22.04_armv8",
            "ubuntu-24.04_armv8",
            "raspberry-pi-os_armv8",
            "ubuntu-20.04_armv8_jetson",
            "ubuntu-22.04_armv8_jetson",
        ):
            conf = os.path.join(BASE_DIR, "multistrap", f"{platform.target.package_name}.conf")
            # conf ファイルのハッシュ値をバージョンとする
            version_md5 = hashlib.md5(open(conf, "rb").read()).hexdigest()
            install_rootfs_args = {
                "version": version_md5,
                "version_file": os.path.join(install_dir, "rootfs.version"),
                "install_dir": install_dir,
                "conf": conf,
            }
            install_rootfs(**install_rootfs_args)

        # Android NDK
        if platform.target.os == "android":
            install_android_ndk_args = {
                "version": deps["ANDROID_NDK_VERSION"],
                "version_file": os.path.join(install_dir, "android-ndk.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
            }
            install_android_ndk(**install_android_ndk_args)

        # Android SDK Commandline Tools
        if platform.target.os == "android":
            if "ANDROID_SDK_ROOT" in os.environ and os.path.exists(os.environ["ANDROID_SDK_ROOT"]):
                # 既に Android SDK が設定されている場合はインストールしない
                pass
            else:
                install_android_sdk_cmdline_tools_args = {
                    "version": deps["ANDROID_SDK_CMDLINE_TOOLS_VERSION"],
                    "version_file": os.path.join(install_dir, "android-sdk-cmdline-tools.version"),
                    "source_dir": source_dir,
                    "install_dir": install_dir,
                }
                install_android_sdk_cmdline_tools(**install_android_sdk_cmdline_tools_args)
                add_path(
                    os.path.join(install_dir, "android-sdk-cmdline-tools", "cmdline-tools", "bin")
                )
                os.environ["ANDROID_SDK_ROOT"] = os.path.join(
                    install_dir, "android-sdk-cmdline-tools"
                )

        # WebRTC
        webrtc_platform = get_webrtc_platform(platform)

        if local_webrtc_build_dir is None:
            install_webrtc_args = {
                "version": deps["WEBRTC_BUILD_VERSION"],
                "version_file": os.path.join(install_dir, "webrtc.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
                "platform": webrtc_platform,
            }

            install_webrtc(**install_webrtc_args)
        else:
            build_webrtc_args = {
                "platform": webrtc_platform,
                "local_webrtc_build_dir": local_webrtc_build_dir,
                "local_webrtc_build_args": local_webrtc_build_args,
                "debug": debug,
            }

            build_webrtc(**build_webrtc_args)

        webrtc_info = get_webrtc_info(webrtc_platform, local_webrtc_build_dir, install_dir, debug)
        webrtc_version = read_version_file(webrtc_info.version_file)
        webrtc_deps = read_version_file(webrtc_info.deps_file)

        # Windows は MSVC を使うので不要
        if platform.target.os not in ("windows",):

            def get_webrtc_value(key: str) -> Optional[str]:
                return webrtc_version.get(key) or webrtc_deps.get(key)

            # LLVM
            tools_url = get_webrtc_value("WEBRTC_SRC_TOOLS_URL")
            tools_commit = get_webrtc_value("WEBRTC_SRC_TOOLS_COMMIT")
            libcxx_url = get_webrtc_value("WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_URL")
            libcxx_commit = get_webrtc_value("WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT")
            buildtools_url = get_webrtc_value("WEBRTC_SRC_BUILDTOOLS_URL")
            buildtools_commit = get_webrtc_value("WEBRTC_SRC_BUILDTOOLS_COMMIT")
            missing = [
                key
                for key, value in (
                    ("WEBRTC_SRC_TOOLS_URL", tools_url),
                    ("WEBRTC_SRC_TOOLS_COMMIT", tools_commit),
                    ("WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_URL", libcxx_url),
                    ("WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT", libcxx_commit),
                    ("WEBRTC_SRC_BUILDTOOLS_URL", buildtools_url),
                    ("WEBRTC_SRC_BUILDTOOLS_COMMIT", buildtools_commit),
                )
                if value is None
            ]
            if missing:
                if webrtc_info.webrtc_source_dir is None:
                    raise RuntimeError(
                        "Missing LLVM metadata in VERSION/DEPS: " + ", ".join(missing)
                    )
                logging.info("LLVM metadata missing; installing LLVM from local WebRTC source.")
                install_llvm_from_webrtc_source(webrtc_info.webrtc_source_dir, install_dir)
            else:
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

        # Boost
        install_boost_args = {
            "version": deps["BOOST_VERSION"],
            "version_file": os.path.join(install_dir, "boost.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "expected_sha256": deps["BOOST_SHA256_HASH"],
            "cxx": "",
            "cflags": [],
            "cxxflags": [],
            "linkflags": [],
            "toolset": "",
            "visibility": "global",
            "target_os": "",
            "debug": debug,
            "android_ndk": "",
            "native_api_level": "",
            "architecture": "x86",
        }
        if platform.target.os == "windows":
            install_boost_args["cxxflags"] = ["-D_ITERATOR_DEBUG_LEVEL=0"]
            install_boost_args["toolset"] = "msvc"
            install_boost_args["target_os"] = "windows"
        elif platform.target.os == "macos":
            sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
            install_boost_args["target_os"] = "darwin"
            install_boost_args["toolset"] = "clang"
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cflags"] = [
                f"--sysroot={sysroot}",
                f"-mmacosx-version-min={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}",
            ]
            install_boost_args["cxxflags"] = [
                "-fPIC",
                f"--sysroot={sysroot}",
                "-std=gnu++17",
                f"-mmacosx-version-min={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}",
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
            ]
            install_boost_args["visibility"] = "hidden"
            if platform.target.arch == "x86_64":
                install_boost_args["cflags"].extend(["-target", "x86_64-apple-darwin"])
                install_boost_args["cxxflags"].extend(["-target", "x86_64-apple-darwin"])
                install_boost_args["architecture"] = "x86"
            if platform.target.arch == "arm64":
                install_boost_args["cflags"].extend(["-target", "aarch64-apple-darwin"])
                install_boost_args["cxxflags"].extend(["-target", "aarch64-apple-darwin"])
                install_boost_args["architecture"] = "arm"
        elif platform.target.os in ("jetson", "raspberry-pi-os"):
            triplet = "aarch64-linux-gnu"
            sysroot = os.path.join(install_dir, "rootfs")
            install_boost_args["target_os"] = "linux"
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cflags"] = [
                "-fPIC",
                f"--sysroot={sysroot}",
                f"--target={triplet}",
                f"-I{os.path.join(sysroot, 'usr', 'include', triplet)}",
            ]
            install_boost_args["cxxflags"] = [
                "-fPIC",
                f"--target={triplet}",
                f"--sysroot={sysroot}",
                f"-I{os.path.join(sysroot, 'usr', 'include', triplet)}",
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                "-std=gnu++17",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
            ]
            install_boost_args["linkflags"] = [
                f"-L{os.path.join(sysroot, 'usr', 'lib', triplet)}",
                f"-B{os.path.join(sysroot, 'usr', 'lib', triplet)}",
            ]
            install_boost_args["toolset"] = "clang"
            install_boost_args["architecture"] = "arm"
        elif platform.target.os == "ios":
            install_boost_args["target_os"] = "iphone"
            install_boost_args["toolset"] = "clang"
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cflags"] = [
                f"-miphoneos-version-min={webrtc_deps['IOS_DEPLOYMENT_TARGET']}",
            ]
            install_boost_args["cxxflags"] = [
                "-std=gnu++17",
                f"-miphoneos-version-min={webrtc_deps['IOS_DEPLOYMENT_TARGET']}",
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
            ]
            install_boost_args["visibility"] = "hidden"
        elif platform.target.os == "android":
            install_boost_args["target_os"] = "android"
            install_boost_args["cflags"] = [
                "-fPIC",
            ]
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cxxflags"] = [
                "-fPIC",
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                "-std=gnu++17",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
            ]
            install_boost_args["toolset"] = "clang"
            install_boost_args["android_ndk"] = os.path.join(install_dir, "android-ndk")
            install_boost_args["native_api_level"] = deps["ANDROID_NATIVE_API_LEVEL"]
            install_boost_args["architecture"] = (
                "x86" if platform.target.arch == "x86_64" else "arm"
            )
        else:
            install_boost_args["target_os"] = "linux"
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cxxflags"] = [
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                "-std=gnu++17",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
                "-fPIC",
            ]
            install_boost_args["toolset"] = "clang"
            if platform.target.arch == "armv8":
                sysroot = os.path.join(install_dir, "rootfs")
                install_boost_args["cflags"] += [
                    f"--sysroot={sysroot}",
                    "--target=aarch64-linux-gnu",
                    f"-I{os.path.join(sysroot, 'usr', 'include', 'aarch64-linux-gnu')}",
                ]
                install_boost_args["cxxflags"] += [
                    f"--sysroot={sysroot}",
                    "--target=aarch64-linux-gnu",
                    f"-I{os.path.join(sysroot, 'usr', 'include', 'aarch64-linux-gnu')}",
                ]
                install_boost_args["linkflags"] += [
                    f"-L{os.path.join(sysroot, 'usr', 'lib', 'aarch64-linux-gnu')}",
                    f"-B{os.path.join(sysroot, 'usr', 'lib', 'aarch64-linux-gnu')}",
                ]
                install_boost_args["architecture"] = "arm"

        build_and_install_boost(**install_boost_args)

        # CMake
        install_cmake_args = {
            "version": deps["CMAKE_VERSION"],
            "version_file": os.path.join(install_dir, "cmake.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "",
            "ext": "tar.gz",
        }
        if platform.build.os == "windows" and platform.build.arch == "x86_64":
            install_cmake_args["platform"] = "windows-x86_64"
            install_cmake_args["ext"] = "zip"
        elif platform.build.os == "macos":
            install_cmake_args["platform"] = "macos-universal"
        elif platform.build.os == "ubuntu" and platform.build.arch == "x86_64":
            install_cmake_args["platform"] = "linux-x86_64"
        elif platform.build.os == "ubuntu" and platform.build.arch == "arm64":
            install_cmake_args["platform"] = "linux-aarch64"
        else:
            raise Exception("Failed to install CMake")
        install_cmake(**install_cmake_args)

        if platform.build.os == "macos":
            add_path(os.path.join(install_dir, "cmake", "CMake.app", "Contents", "bin"))
        else:
            add_path(os.path.join(install_dir, "cmake", "bin"))

        # CUDA
        if not disable_cuda and platform.target.os == "windows":
            install_cuda_args = {
                "version": deps["CUDA_VERSION"],
                "version_file": os.path.join(install_dir, "cuda.version"),
                "source_dir": source_dir,
                "build_dir": build_dir,
                "install_dir": install_dir,
            }
            install_cuda_windows(**install_cuda_args)

        # Intel VPL
        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            install_vpl_args = {
                "version": deps["VPL_VERSION"],
                "version_file": os.path.join(install_dir, "vpl.version"),
                "configuration": "Debug" if debug else "Release",
                "source_dir": source_dir,
                "build_dir": build_dir,
                "install_dir": install_dir,
                "cmake_args": [],
            }
            if platform.target.os == "windows":
                cxxflags = [
                    "/DWIN32",
                    "/D_WINDOWS",
                    "/W3",
                    "/GR",
                    "/EHsc",
                    "/D_ITERATOR_DEBUG_LEVEL=0",
                ]
                install_vpl_args["cmake_args"].append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
            if platform.target.os == "ubuntu":
                apt_install_llvm_version = deps["APT_INSTALL_LLVM_VERSION"]
                cmake_args = []
                cmake_args.append(f"-DCMAKE_C_COMPILER=clang-{apt_install_llvm_version}")
                cmake_args.append(f"-DCMAKE_CXX_COMPILER=clang++-{apt_install_llvm_version}")
                path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
                cmake_args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
                flags = [
                    "-nostdinc++",
                    "-D_LIBCPP_ABI_NAMESPACE=Cr",
                    "-D_LIBCPP_ABI_VERSION=2",
                    "-D_LIBCPP_DISABLE_AVAILABILITY",
                    "-D_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS",
                    "-D_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS",
                    "-D_LIBCPP_ENABLE_NODISCARD",
                    "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                ]
                cmake_args.append(f"-DCMAKE_CXX_FLAGS={' '.join(flags)}")
                install_vpl_args["cmake_args"] += cmake_args
            install_vpl(**install_vpl_args)

        # AMF
        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            install_amf_args = {
                "version": deps["AMF_VERSION"],
                "version_file": os.path.join(install_dir, "amf.version"),
                "install_dir": install_dir,
            }
            install_amf(**install_amf_args)

        # OpenH264
        install_openh264_args = {
            "version": deps["OPENH264_VERSION"],
            "version_file": os.path.join(install_dir, "openh264.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "is_windows": platform.target.os == "windows",
        }
        install_openh264(**install_openh264_args)

        if platform.target.os == "android":
            # Android 側からのコールバックする関数は消してはいけないので、
            # libwebrtc.a の中から消してはいけない関数の一覧を作っておく
            #
            # readelf を使って libwebrtc.a の関数一覧を列挙して、その中から Java_org_webrtc_ を含む関数を取り出し、
            # -Wl,--undefined=<関数名> に加工する。
            # （-Wl,--undefined はアプリケーションから参照されていなくても関数を削除しないためのフラグ）
            readelf = os.path.join(
                install_dir,
                "android-ndk",
                "toolchains",
                "llvm",
                "prebuilt",
                "linux-x86_64",
                "bin",
                "llvm-readelf",
            )
            android_abi = get_android_abi(platform)
            libwebrtc = os.path.join(webrtc_info.webrtc_library_dir, android_abi, "libwebrtc.a")
            m = cmdcap([readelf, "-Ws", libwebrtc])
            ldflags = []
            for line in m.splitlines():
                if line.find("Java_org_webrtc_") == -1:
                    continue
                # この時点で line は以下のような文字列になっている
                #    174: 0000000000000000    44 FUNC    GLOBAL DEFAULT    15 Java_org_webrtc_DataChannel_nativeClose
                func = line.split()[7]
                ldflags.append(f"-Wl,--undefined={func}")
            with open(os.path.join(install_dir, "webrtc.ldflags"), "w") as f:
                f.write("\n".join(ldflags))

        # Blend2D
        install_blend2d_args = {
            "version": deps["BLEND2D_VERSION"],
            "version_file": os.path.join(install_dir, "blend2d.version"),
            "configuration": "Debug" if debug else "Release",
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "cmake_args": [],
            "expected_sha256": deps["BLEND2D_SHA256_HASH"],
        }
        install_blend2d_args["cmake_args"] = get_common_cmake_args(
            platform, deps, webrtc_info, BASE_DIR, install_dir, debug
        )
        install_blend2d_official(**install_blend2d_args)

        # テストできる環境だけ入れる
        if platform.build.os == platform.target.os and platform.build.arch == platform.target.arch:
            # Catch2
            install_catch2_args = {
                "version": deps["CATCH2_VERSION"],
                "version_file": os.path.join(install_dir, "catch2.version"),
                "source_dir": source_dir,
                "build_dir": build_dir,
                "install_dir": install_dir,
                "configuration": "Debug" if debug else "Release",
                "cmake_args": [],
            }
            install_catch2_args["cmake_args"] = get_common_cmake_args(
                platform, deps, webrtc_info, BASE_DIR, install_dir, debug
            )
            install_catch2(**install_catch2_args)


def check_version_file():
    sora_cpp_sdk_version = read_version(os.path.join(BASE_DIR, "VERSION"))
    deps = read_version_file(os.path.join(BASE_DIR, "DEPS"))
    example_deps = read_version_file(os.path.join(BASE_DIR, "examples", "DEPS"))
    has_error = False
    # VERSION ファイルに書いてる Sora C++ SDK のバージョンが、
    # examples/DEPS にある SORA_CPP_SDK_VERSION と一致しているか確認
    if sora_cpp_sdk_version != example_deps["SORA_CPP_SDK_VERSION"]:
        logging.error(
            f"SORA_CPP_SDK_VERSION mismatch: VERSION={sora_cpp_sdk_version}, examples/DEPS={example_deps['SORA_CPP_SDK_VERSION']}"
        )
        has_error = True
    # その他のバージョンも確認
    if deps["WEBRTC_BUILD_VERSION"] != example_deps["WEBRTC_BUILD_VERSION"]:
        logging.error(
            f"WEBRTC_BUILD_VERSION mismatch: DEPS={deps['WEBRTC_BUILD_VERSION']}, examples/DEPS={example_deps['WEBRTC_BUILD_VERSION']}"
        )
        has_error = True
    if deps["BOOST_VERSION"] != example_deps["BOOST_VERSION"]:
        logging.error(
            f"BOOST_VERSION mismatch: DEPS={deps['BOOST_VERSION']}, examples/DEPS={example_deps['BOOST_VERSION']}"
        )
        has_error = True
    if has_error:
        raise Exception("VERSION/DEPS mismatch")


AVAILABLE_TARGETS = [
    "windows_x86_64",
    "macos_arm64",
    "ubuntu-22.04_x86_64",
    "ubuntu-24.04_x86_64",
    "ubuntu-22.04_armv8",
    "ubuntu-24.04_armv8",
    "raspberry-pi-os_armv8",
    "ios",
    "android",
    "android_x86_64",
]
WINDOWS_SDK_VERSION = "10.0.20348.0"


def _get_platform(target: str) -> Platform:
    if target == "windows_x86_64":
        platform = Platform("windows", get_windows_osver(), "x86_64")
    elif target == "macos_arm64":
        platform = Platform("macos", get_macos_osver(), "arm64")
    elif target == "ubuntu-22.04_x86_64":
        platform = Platform("ubuntu", "22.04", "x86_64")
    elif target == "ubuntu-24.04_x86_64":
        platform = Platform("ubuntu", "24.04", "x86_64")
    elif target == "ubuntu-22.04_armv8":
        platform = Platform("ubuntu", "22.04", "armv8")
    elif target == "ubuntu-24.04_armv8":
        platform = Platform("ubuntu", "24.04", "armv8")
    elif target == "raspberry-pi-os_armv8":
        platform = Platform("raspberry-pi-os", None, "armv8")
    elif target == "ios":
        platform = Platform("ios", None, None)
    elif target == "android":
        platform = Platform("android", None, "arm64")
    elif target == "android_x86_64":
        platform = Platform("android", None, "x86_64")
    else:
        raise Exception(f"Unknown target {target}")
    return platform


def _build(
    target: str,
    debug: bool,
    relwithdebinfo: bool,
    local_webrtc_build_dir: Optional[str],
    local_webrtc_build_args: List[str],
    disable_cuda: bool,
    test: bool,
    run_e2e_test: bool,
    package: bool,
):
    platform = _get_platform(target)

    logging.info(f"Build platform: {platform.build.package_name}")
    logging.info(f"Target platform: {platform.target.package_name}")

    configuration = "debug" if debug else "release"
    dir = platform.target.package_name
    source_dir = os.path.join(BASE_DIR, "_source", dir, configuration)
    build_dir = os.path.join(BASE_DIR, "_build", dir, configuration)
    install_dir = os.path.join(BASE_DIR, "_install", dir, configuration)
    package_dir = os.path.join(BASE_DIR, "_package", dir, configuration)
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(
        platform,
        source_dir,
        build_dir,
        install_dir,
        debug,
        local_webrtc_build_dir=local_webrtc_build_dir,
        local_webrtc_build_args=local_webrtc_build_args,
        disable_cuda=disable_cuda,
    )

    configuration = "Release"
    if debug:
        configuration = "Debug"
    if relwithdebinfo:
        configuration = "RelWithDebInfo"

    sora_build_dir = os.path.join(build_dir, "sora")
    mkdir_p(sora_build_dir)
    with cd(sora_build_dir):
        cmake_args = []
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        cmake_args.append(f"-DCMAKE_INSTALL_PREFIX={cmake_path(os.path.join(install_dir, 'sora'))}")
        cmake_args.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(os.path.join(install_dir, 'boost'))}")
        webrtc_platform = get_webrtc_platform(platform)
        webrtc_info = get_webrtc_info(webrtc_platform, local_webrtc_build_dir, install_dir, debug)
        webrtc_version = read_version_file(webrtc_info.version_file)
        webrtc_deps = read_version_file(webrtc_info.deps_file)
        with cd(BASE_DIR):
            sora_cpp_sdk_version = read_version("VERSION")
            deps = read_version_file("DEPS")
            sora_cpp_sdk_commit = cmdcap(["git", "rev-parse", "HEAD"])
            android_native_api_level = deps["ANDROID_NATIVE_API_LEVEL"]
            apt_install_llvm_version = deps["APT_INSTALL_LLVM_VERSION"]
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}")
        cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}")
        cmake_args.append(f"-DSORA_CPP_SDK_VERSION={sora_cpp_sdk_version}")
        cmake_args.append(f"-DSORA_CPP_SDK_COMMIT={sora_cpp_sdk_commit}")
        cmake_args.append(f"-DSORA_CPP_SDK_TARGET={platform.target.package_name}")
        cmake_args.append(f"-DWEBRTC_BUILD_VERSION={webrtc_version['WEBRTC_BUILD_VERSION']}")
        cmake_args.append(f"-DWEBRTC_READABLE_VERSION={webrtc_version['WEBRTC_READABLE_VERSION']}")
        cmake_args.append(f"-DWEBRTC_COMMIT={webrtc_version['WEBRTC_COMMIT']}")
        cmake_args.append(f"-DOPENH264_ROOT={cmake_path(os.path.join(install_dir, 'openh264'))}")
        cmake_args.append(f"-DBLEND2D_ROOT_DIR={cmake_path(os.path.join(install_dir, 'blend2d'))}")
        if platform.target.os == "windows":
            cmake_args.append(f"-DCMAKE_SYSTEM_VERSION={WINDOWS_SDK_VERSION}")
        if platform.target.os == "ubuntu":
            if platform.target.package_name in (
                "ubuntu-22.04_x86_64",
                "ubuntu-24.04_x86_64",
            ):
                cmake_args.append(f"-DCMAKE_C_COMPILER=clang-{apt_install_llvm_version}")
                cmake_args.append(f"-DCMAKE_CXX_COMPILER=clang++-{apt_install_llvm_version}")
            else:
                sysroot = os.path.join(install_dir, "rootfs")
                cmake_args.append(
                    f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                )
                cmake_args.append(
                    f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                )
                cmake_args.append("-DCMAKE_SYSTEM_NAME=Linux")
                cmake_args.append("-DCMAKE_SYSTEM_PROCESSOR=aarch64")
                cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
                cmake_args.append("-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu")
                cmake_args.append("-DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu")
                cmake_args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
                cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER")
            cmake_args.append("-DUSE_LIBCXX=ON")
            cmake_args.append(
                f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
            )
        if platform.target.os == "macos":
            sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
            target = (
                "x86_64-apple-darwin"
                if platform.target.arch == "x86_64"
                else "aarch64-apple-darwin"
            )
            cmake_args.append(
                f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
            )
            cmake_args.append(
                f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
            )
            cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={platform.target.arch}")
            cmake_args.append(f"-DCMAKE_OSX_ARCHITECTURES={platform.target.arch}")
            cmake_args.append(
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}"
            )
            cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
            cmake_args.append("-DUSE_LIBCXX=ON")
            cmake_args.append(
                f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
            )
        if platform.target.os in ("jetson", "raspberry-pi-os"):
            triplet = "aarch64-linux-gnu"
            arch = "aarch64"
            sysroot = os.path.join(install_dir, "rootfs")
            cmake_args.append("-DCMAKE_SYSTEM_NAME=Linux")
            cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch}")
            cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
            cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={triplet}")
            cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={triplet}")
            cmake_args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
            cmake_args.append("-DUSE_LIBCXX=ON")
            cmake_args.append(
                f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
            )
            cmake_args.append(
                f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
            )
            cmake_args.append(
                f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
            )
        if platform.target.os == "ios":
            cmake_args.append(
                f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
            )
            cmake_args.append(
                f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
            )
            cmake_args.append("-DCMAKE_SYSTEM_NAME=iOS")
            cmake_args.append("-DCMAKE_OSX_ARCHITECTURES=arm64")
            cmake_args.append(
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['IOS_DEPLOYMENT_TARGET']}"
            )
            cmake_args.append("-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO")
            cmake_args.append("-DUSE_LIBCXX=ON")
            cmake_args.append(
                f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
            )
        if platform.target.os == "android":
            android_ndk = os.path.join(install_dir, "android-ndk")
            toolchain_file = os.path.join(BASE_DIR, "cmake", "android.toolchain.cmake")
            override_toolchain_file = os.path.join(
                os.path.join(android_ndk, "build", "cmake", "android.toolchain.cmake")
            )
            override_c_compiler = os.path.join(webrtc_info.clang_dir, "bin", "clang")
            override_cxx_compiler = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            android_clang_dir = os.path.join(
                android_ndk, "toolchains", "llvm", "prebuilt", "linux-x86_64"
            )
            android_abi = get_android_abi(platform)
            clang_lib_arch = get_android_clang_lib_arch(platform)
            sysroot = os.path.join(android_clang_dir, "sysroot")
            cmake_args.append(
                f"-DANDROID_OVERRIDE_TOOLCHAIN_FILE={cmake_path(override_toolchain_file)}"
            )
            cmake_args.append(f"-DANDROID_OVERRIDE_C_COMPILER={cmake_path(override_c_compiler)}")
            cmake_args.append(
                f"-DANDROID_OVERRIDE_CXX_COMPILER={cmake_path(override_cxx_compiler)}"
            )
            cmake_args.append(f"-DCMAKE_SYSROOT={cmake_path(sysroot)}")
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={cmake_path(toolchain_file)}")
            cmake_args.append(f"-DANDROID_NATIVE_API_LEVEL={android_native_api_level}")
            cmake_args.append(f"-DANDROID_PLATFORM={android_native_api_level}")
            cmake_args.append(f"-DANDROID_ABI={android_abi}")
            cmake_args.append("-DANDROID_STL=none")
            cmake_args.append("-DUSE_LIBCXX=ON")
            cmake_args.append(
                f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
            )
            cmake_args.append("-DANDROID_CPP_FEATURES=exceptions rtti")
            # r23b には ANDROID_CPP_FEATURES=exceptions でも例外が設定されない問題がある
            # https://github.com/android/ndk/issues/1618
            cmake_args.append("-DCMAKE_ANDROID_EXCEPTIONS=ON")
            cmake_args.append("-DANDROID_NDK=OFF")
            cmake_args.append(
                f"-DSORA_WEBRTC_LDFLAGS={os.path.join(install_dir, 'webrtc.ldflags')}"
            )
            clang_version = fix_clang_version(
                android_clang_dir,
                get_clang_version(os.path.join(android_clang_dir, "bin", "clang++")),
            )
            ldflags = [
                f"-L{cmake_path(os.path.join(android_clang_dir, 'lib', 'clang', clang_version, 'lib', 'linux', clang_lib_arch))}"
            ]
            cmake_args.append(f"-DCMAKE_EXE_LINKER_FLAGS={' '.join(ldflags)}")

        # NvCodec
        if not disable_cuda:
            if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
                cmake_args.append("-DUSE_NVCODEC_ENCODER=ON")
                if platform.target.os == "windows":
                    cmake_args.append(
                        f"-DCUDA_TOOLKIT_ROOT_DIR={cmake_path(os.path.join(install_dir, 'cuda'))}"
                    )

        # VPL
        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            cmake_args.append("-DUSE_VPL_ENCODER=ON")
            cmake_args.append(f"-DVPL_ROOT_DIR={cmake_path(os.path.join(install_dir, 'vpl'))}")

        # AMF
        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            cmake_args.append("-DUSE_AMF_ENCODER=ON")
            cmake_args.append(f"-DAMF_ROOT_DIR={cmake_path(os.path.join(install_dir, 'amf'))}")

        # Jetson
        if platform.target.os in ("jetson",):
            cmake_args.append("-DUSE_JETSON_ENCODER=ON")

        # V4L2
        if platform.target.os in ("raspberry-pi-os",):
            cmake_args.append("-DUSE_V4L2_ENCODER=ON")

        # バンドルされたライブラリを消しておく
        # （CMake でうまく依存関係を解消できなくて更新されないため）
        rm_rf(os.path.join(sora_build_dir, "bundled"))
        rm_rf(os.path.join(sora_build_dir, "libsora.a"))

        cmd(["cmake", BASE_DIR] + cmake_args)
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
        cmd(["cmake", "--install", ".", "--config", configuration])

        # バンドルされたライブラリをインストールする
        if platform.target.os == "windows":
            shutil.copyfile(
                os.path.join(sora_build_dir, "bundled", "sora.lib"),
                os.path.join(install_dir, "sora", "lib", "sora.lib"),
            )
        else:
            shutil.copyfile(
                os.path.join(sora_build_dir, "bundled", "libsora.a"),
                os.path.join(install_dir, "sora", "lib", "libsora.a"),
            )

    if platform.target.os == "android":
        # Android の場合のみライブラリをビルドする
        with cd(os.path.join(BASE_DIR, "android", "Sora")):
            cmd(["./gradlew", "--no-daemon", "assembleRelease"])
            shutil.copyfile(
                os.path.join(
                    BASE_DIR,
                    "android",
                    "Sora",
                    "Sora",
                    "build",
                    "outputs",
                    "aar",
                    "Sora-release.aar",
                ),
                os.path.join(install_dir, "sora", "lib", "Sora.aar"),
            )

    if test:
        if platform.target.os == "ios":
            # iOS の場合は事前に用意したプロジェクトをビルドする
            # → libwebrtc.a から x64 のビルドが無くなったのでとりあえずビルドを諦める
            # cmd(['xcodebuild', 'build',
            #     '-project', 'test/ios/hello.xcodeproj',
            #      '-target', 'hello',
            #      '-arch', 'x86_64',
            #      '-sdk', 'iphonesimulator',
            #      '-configuration', 'Release'])
            # こっちは signing が必要になるのでやらない
            # cmd(['xcodebuild', 'build',
            #      '-project', 'test/ios/hello.xcodeproj',
            #      '-target', 'hello',
            #      '-arch', 'arm64',
            #      '-sdk', 'iphoneos',
            #      '-configuration', 'Release'])
            pass
        elif platform.target.os == "android":
            # Android の場合は事前に用意したプロジェクトをビルドする
            with cd(os.path.join(BASE_DIR, "test", "android")):
                gradle_args = [
                    "./gradlew",
                    "--no-daemon",
                    "assemble",
                    f"-PSORA_ANDROID_ABI={get_android_abi(platform)}",
                ]
                if local_webrtc_build_dir is not None:
                    gradle_args.append(f"-PSORA_WEBRTC_LOCAL_BUILD_DIR={local_webrtc_build_dir}")
                cmd(gradle_args)
        else:
            # 普通のプロジェクトは CMake でビルドする
            test_build_dir = os.path.join(build_dir, "test")
            mkdir_p(test_build_dir)
            with cd(test_build_dir):
                cmake_args = []
                cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
                cmake_args.append("-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
                cmake_args.append(f"-DBOOST_ROOT={cmake_path(os.path.join(install_dir, 'boost'))}")
                cmake_args.append(
                    f"-DCATCH2_ROOT={cmake_path(os.path.join(install_dir, 'catch2'))}"
                )
                cmake_args.append(
                    f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}"
                )
                cmake_args.append(
                    f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}"
                )
                cmake_args.append(f"-DSORA_DIR={cmake_path(os.path.join(install_dir, 'sora'))}")
                if platform.target.os == "macos":
                    sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
                    target = (
                        "x86_64-apple-darwin"
                        if platform.target.arch == "x86_64"
                        else "aarch64-apple-darwin"
                    )
                    cmake_args.append(
                        f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                    )
                    cmake_args.append(
                        f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                    )
                    cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={platform.target.arch}")
                    cmake_args.append(f"-DCMAKE_OSX_ARCHITECTURES={platform.target.arch}")
                    cmake_args.append(
                        f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}"
                    )
                    cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
                    cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
                    cmake_args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
                    cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
                    cmake_args.append("-DUSE_LIBCXX=ON")
                    cmake_args.append(
                        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
                    )
                if platform.target.os == "ubuntu":
                    if platform.target.package_name in (
                        "ubuntu-22.04_x86_64",
                        "ubuntu-24.04_x86_64",
                    ):
                        cmake_args.append(f"-DCMAKE_C_COMPILER=clang-{apt_install_llvm_version}")
                        cmake_args.append(
                            f"-DCMAKE_CXX_COMPILER=clang++-{apt_install_llvm_version}"
                        )
                    else:
                        sysroot = os.path.join(install_dir, "rootfs")
                        cmake_args.append(
                            f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                        )
                        cmake_args.append(
                            f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                        )
                        cmake_args.append("-DCMAKE_SYSTEM_NAME=Linux")
                        cmake_args.append("-DCMAKE_SYSTEM_PROCESSOR=aarch64")
                        cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
                        cmake_args.append("-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu")
                        cmake_args.append("-DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu")
                        cmake_args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
                        cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER")
                        cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH")
                        cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH")
                        cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH")
                    cmake_args.append("-DUSE_LIBCXX=ON")
                    cmake_args.append(
                        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
                    )
                if platform.target.os in ("jetson", "raspberry-pi-os"):
                    triplet = "aarch64-linux-gnu"
                    arch = "aarch64"
                    sysroot = os.path.join(install_dir, "rootfs")
                    cmake_args.append("-DCMAKE_SYSTEM_NAME=Linux")
                    cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch}")
                    cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
                    cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={triplet}")
                    cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={triplet}")
                    cmake_args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
                    cmake_args.append("-DUSE_LIBCXX=ON")
                    cmake_args.append(
                        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
                    )
                    cmake_args.append(
                        f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                    )
                    cmake_args.append(
                        f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                    )
                if platform.target.os in ("windows", "macos", "ubuntu"):
                    cmake_args.append("-DTEST_CONNECT_DISCONNECT=ON")
                    cmake_args.append("-DTEST_DATACHANNEL=ON")
                    cmake_args.append("-DTEST_DEVICE_LIST=ON")
                if (
                    platform.build.os == platform.target.os
                    and platform.build.arch == platform.target.arch
                ):
                    cmake_args.append("-DTEST_E2E=ON")

                cmd(["cmake", os.path.join(BASE_DIR, "test")] + cmake_args)
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

                if platform.target.os == "raspberry-pi-os":
                    # ラズパイでは libcamerac.so を使うので実行ファイルと同じ場所に配置しておく
                    shutil.copyfile(
                        os.path.join(install_dir, "sora", "lib", "libcamerac.so"),
                        os.path.join(test_build_dir, "libcamerac.so"),
                    )

                if run_e2e_test:
                    if (
                        platform.build.os == platform.target.os
                        and platform.build.arch == platform.target.arch
                    ):
                        if platform.target.os == "windows":
                            cmd([os.path.join(test_build_dir, configuration, "e2e.exe")])
                        else:
                            cmd([os.path.join(test_build_dir, "e2e")])

    if package:
        mkdir_p(package_dir)
        rm_rf(os.path.join(package_dir, "sora"))
        rm_rf(os.path.join(package_dir, "sora.env"))

        with cd(BASE_DIR):
            sora_cpp_sdk_version = read_version("VERSION")
            deps = read_version_file("DEPS")
            boost_version = deps["BOOST_VERSION"]

        def archive(archive_path, files, is_windows):
            if is_windows:
                with zipfile.ZipFile(archive_path, "w") as f:
                    for file in files:
                        f.write(filename=file, arcname=file)
            else:
                with tarfile.open(archive_path, "w:gz") as f:
                    for file in files:
                        f.add(name=file, arcname=file)

        ext = "zip" if platform.target.os == "windows" else "tar.gz"
        is_windows = platform.target.os == "windows"
        content_type = "application/zip" if platform.target.os == "windows" else "application/gzip"

        with cd(install_dir):
            archive_name = (
                f"sora-cpp-sdk-{sora_cpp_sdk_version}_{platform.target.package_name}.{ext}"
            )
            archive_path = os.path.join(package_dir, archive_name)
            archive(archive_path, enum_all_files("sora", "."), is_windows)

            boost_archive_name = f"boost-{boost_version}_sora-cpp-sdk-{sora_cpp_sdk_version}_{platform.target.package_name}.{ext}"
            boost_archive_path = os.path.join(package_dir, boost_archive_name)
            archive(boost_archive_path, enum_all_files("boost", "."), is_windows)

            with open(os.path.join(package_dir, "sora.env"), "w") as f:
                f.write(f"CONTENT_TYPE={content_type}\n")
                f.write(f"PACKAGE_NAME={archive_name}\n")
                f.write(f"BOOST_PACKAGE_NAME={boost_archive_name}\n")


def _find_clang_binary(name: str) -> Optional[str]:
    for n in range(50, 14, -1):
        if shutil.which(f"{name}-{n}") is not None:
            return f"{name}-{n}"
    else:
        if shutil.which(name) is not None:
            return name
    return None


def _do_iwyu(
    clang_scan_deps_path: str,
    clang_include_cleaner_path: str,
    patterns: List[str],
    compile_commands_path: str,
    include_cleaner_info_path: str,
):
    with cd(BASE_DIR):
        if not os.path.exists(compile_commands_path):
            logging.warning(f"Compile commands file not found: {compile_commands_path}")
            return

        target_files = set()
        for pattern in patterns:
            files = glob.glob(pattern, recursive=True)
            for file in files:
                target_files.add(os.path.join(BASE_DIR, file))

        # clang-scan-deps を使って .h と .cpp ファイルを収集
        scan_file_set = set()
        sora_deps = json.loads(
            cmdcap(
                [
                    clang_scan_deps_path,
                    "-compilation-database",
                    compile_commands_path,
                    "-format",
                    "experimental-full",
                ]
            )
        )
        for unit in sora_deps["translation-units"]:
            for command in unit["commands"]:
                if command["input-file"] in target_files:
                    scan_file_set.add(command["input-file"])
                for dep in command["file-deps"]:
                    if dep in target_files:
                        scan_file_set.add(dep)

        if os.path.exists(include_cleaner_info_path):
            include_cleaner_info = json.load(open(include_cleaner_info_path, "r", encoding="utf-8"))
        else:
            include_cleaner_info = {}

        scan_files = sorted(scan_file_set)
        try:
            for file in scan_files:
                digest = hashlib.sha256(open(file, "rb").read()).hexdigest()
                if file in include_cleaner_info and include_cleaner_info[file] == digest:
                    logging.info(f"Skipping {file} (already processed)")
                    continue

                logging.info(f"Processing {file} with clang-include-cleaner")
                cmd(
                    [
                        clang_include_cleaner_path,
                        "-p",
                        os.path.dirname(compile_commands_path),
                        "--edit",
                        file,
                    ]
                )
                include_cleaner_info[file] = digest
        finally:
            with open(include_cleaner_info_path, "w", encoding="utf-8") as f:
                json.dump(include_cleaner_info, f, indent=2, ensure_ascii=False)


def _iwyu(
    target: str,
    debug: bool,
    relwithdebinfo: bool,
    clang_scan_deps_path: Optional[str] = None,
    clang_include_cleaner_path: Optional[str] = None,
):
    platform = _get_platform(target)
    configuration = "debug" if debug else "release"
    build_dir = os.path.join(BASE_DIR, "_build", platform.target.package_name, configuration)
    example_build_dir = os.path.join(
        BASE_DIR, "examples", "_build", platform.target.package_name, configuration
    )

    if clang_scan_deps_path is None:
        clang_scan_deps_path = _find_clang_binary("clang-scan-deps")
    if clang_include_cleaner_path is None:
        clang_include_cleaner_path = _find_clang_binary("clang-include-cleaner")

    if clang_scan_deps_path is None:
        raise Exception("clang-scan-deps not found. Please install it or specify the path.")
    if clang_include_cleaner_path is None:
        raise Exception("clang-include-cleaner not found. Please install it or specify the path.")

    include_cleaner_info_path = os.path.join(build_dir, "clang-include-cleaner.json")

    _do_iwyu(
        clang_scan_deps_path=clang_scan_deps_path,
        clang_include_cleaner_path=clang_include_cleaner_path,
        patterns=[
            "include/**/*.h",
            "include/**/*.cpp",
            "src/**/*.h",
            "src/**/*.cpp",
        ],
        compile_commands_path=os.path.join(build_dir, "sora", "compile_commands.json"),
        include_cleaner_info_path=include_cleaner_info_path,
    )
    _do_iwyu(
        clang_scan_deps_path=clang_scan_deps_path,
        clang_include_cleaner_path=clang_include_cleaner_path,
        patterns=[
            "test/**/*.h",
            "test/**/*.cpp",
        ],
        compile_commands_path=os.path.join(build_dir, "test", "compile_commands.json"),
        include_cleaner_info_path=include_cleaner_info_path,
    )
    _do_iwyu(
        clang_scan_deps_path=clang_scan_deps_path,
        clang_include_cleaner_path=clang_include_cleaner_path,
        patterns=[
            "examples/messaging_recvonly_sample/**/*.h",
            "examples/messaging_recvonly_sample/**/*.cpp",
        ],
        compile_commands_path=os.path.join(
            example_build_dir, "messaging_recvonly_sample", "compile_commands.json"
        ),
        include_cleaner_info_path=include_cleaner_info_path,
    )
    _do_iwyu(
        clang_scan_deps_path=clang_scan_deps_path,
        clang_include_cleaner_path=clang_include_cleaner_path,
        patterns=[
            "examples/sdl_sample/**/*.h",
            "examples/sdl_sample/**/*.cpp",
        ],
        compile_commands_path=os.path.join(
            example_build_dir, "sdl_sample", "compile_commands.json"
        ),
        include_cleaner_info_path=include_cleaner_info_path,
    )
    _do_iwyu(
        clang_scan_deps_path=clang_scan_deps_path,
        clang_include_cleaner_path=clang_include_cleaner_path,
        patterns=[
            "examples/sumomo/**/*.h",
            "examples/sumomo/**/*.cpp",
        ],
        compile_commands_path=os.path.join(example_build_dir, "sumomo", "compile_commands.json"),
        include_cleaner_info_path=include_cleaner_info_path,
    )


def _format(
    clang_format_path: Optional[str] = None,
):
    if clang_format_path is None:
        clang_format_path = _find_clang_binary("clang-format")
    if clang_format_path is None:
        raise Exception("clang-format not found. Please install it or specify the path.")
    patterns = [
        "include/**/*.h",
        "include/**/*.cpp",
        "src/**/*.h",
        "src/**/*.cpp",
        "src/**/*.mm",
        "test/**/*.h",
        "test/**/*.cpp",
        "examples/messaging_recvonly_sample/**/*.h",
        "examples/messaging_recvonly_sample/**/*.cpp",
        "examples/sdl_sample/**/*.h",
        "examples/sdl_sample/**/*.cpp",
        "examples/sumomo/**/*.h",
        "examples/sumomo/**/*.cpp",
    ]
    target_files = []
    for pattern in patterns:
        files = glob.glob(pattern, recursive=True)
        target_files.extend(files)
    cmd([clang_format_path, "-i"] + target_files)


def main():
    check_version_file()

    parser = argparse.ArgumentParser()
    sp = parser.add_subparsers()
    bp = sp.add_parser("build")
    bp.set_defaults(op="build")
    bp.add_argument("target", choices=AVAILABLE_TARGETS)
    bp.add_argument("--debug", action="store_true")
    bp.add_argument("--relwithdebinfo", action="store_true")
    bp.add_argument("--disable-cuda", action="store_true")
    add_webrtc_build_arguments(bp)
    bp.add_argument("--test", action="store_true")
    bp.add_argument("--run-e2e-test", action="store_true")
    bp.add_argument("--package", action="store_true")
    ip = sp.add_parser("iwyu")
    ip.set_defaults(op="iwyu")
    ip.add_argument("target", choices=AVAILABLE_TARGETS)
    ip.add_argument("--debug", action="store_true")
    ip.add_argument("--relwithdebinfo", action="store_true")
    ip.add_argument("--clang-scan-deps-path", type=str, default=None)
    ip.add_argument("--clang-include-cleaner-path", type=str, default=None)
    fp = sp.add_parser("format")
    fp.set_defaults(op="format")
    fp.add_argument("--clang-format-path", type=str, default=None)

    args = parser.parse_args()

    if args.op == "build":
        _build(
            target=args.target,
            debug=args.debug,
            relwithdebinfo=args.relwithdebinfo,
            local_webrtc_build_dir=args.local_webrtc_build_dir,
            local_webrtc_build_args=args.local_webrtc_build_args,
            disable_cuda=args.disable_cuda,
            test=args.test,
            run_e2e_test=args.run_e2e_test,
            package=args.package,
        )
    elif args.op == "iwyu":
        _iwyu(
            target=args.target,
            debug=args.debug,
            relwithdebinfo=args.relwithdebinfo,
            clang_scan_deps_path=args.clang_scan_deps_path,
            clang_include_cleaner_path=args.clang_include_cleaner_path,
        )
    elif args.op == "format":
        _format(
            clang_format_path=args.clang_format_path,
        )


if __name__ == "__main__":
    main()
