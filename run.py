import argparse
import hashlib
import logging
import multiprocessing
import os
import shutil
import tarfile
import zipfile
from typing import List, Optional

from buildbase import (
    Platform,
    add_path,
    add_webrtc_build_arguments,
    build_and_install_boost,
    build_webrtc,
    cd,
    cmake_path,
    cmd,
    cmdcap,
    enum_all_files,
    get_macos_osver,
    get_webrtc_info,
    get_webrtc_platform,
    get_windows_osver,
    install_android_ndk,
    install_android_sdk_cmdline_tools,
    install_blend2d,
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


def install_deps(
    platform: Platform,
    source_dir: str,
    build_dir: str,
    install_dir: str,
    debug: bool,
    webrtc_build_dir: Optional[str],
    webrtc_build_args: List[str],
):
    with cd(BASE_DIR):
        version = read_version_file("VERSION")

        # multistrap を使った sysroot の構築
        if platform.target.os == "jetson":
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
                "version": version["ANDROID_NDK_VERSION"],
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
                    "version": version["ANDROID_SDK_CMDLINE_TOOLS_VERSION"],
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

        if webrtc_build_dir is None:
            install_webrtc_args = {
                "version": version["WEBRTC_BUILD_VERSION"],
                "version_file": os.path.join(install_dir, "webrtc.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
                "platform": webrtc_platform,
            }

            install_webrtc(**install_webrtc_args)
        else:
            build_webrtc_args = {
                "platform": webrtc_platform,
                "webrtc_build_dir": webrtc_build_dir,
                "webrtc_build_args": webrtc_build_args,
                "debug": debug,
            }

            build_webrtc(**build_webrtc_args)

        webrtc_info = get_webrtc_info(webrtc_platform, webrtc_build_dir, install_dir, debug)
        webrtc_version = read_version_file(webrtc_info.version_file)
        webrtc_deps = read_version_file(webrtc_info.deps_file)

        # Windows は MSVC を使うので不要
        # macOS と iOS は Apple Clang を使うので不要
        if platform.target.os not in ("windows", "macos", "ios") and webrtc_build_dir is None:
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

        # Boost
        install_boost_args = {
            "version": version["BOOST_VERSION"],
            "version_file": os.path.join(install_dir, "boost.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
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
            install_boost_args["cxx"] = "clang++"
            install_boost_args["cflags"] = [
                f"--sysroot={sysroot}",
                f"-mmacosx-version-min={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}",
            ]
            install_boost_args["cxxflags"] = [
                "-fPIC",
                f"--sysroot={sysroot}",
                "-std=gnu++17",
                f"-mmacosx-version-min={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}",
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
        elif platform.target.os == "ios":
            install_boost_args["target_os"] = "iphone"
            install_boost_args["toolset"] = "clang"
            install_boost_args["cflags"] = [
                f"-miphoneos-version-min={webrtc_deps['IOS_DEPLOYMENT_TARGET']}",
            ]
            install_boost_args["cxxflags"] = [
                "-std=gnu++17",
                f"-miphoneos-version-min={webrtc_deps['IOS_DEPLOYMENT_TARGET']}",
            ]
            install_boost_args["visibility"] = "hidden"
        elif platform.target.os == "android":
            install_boost_args["target_os"] = "android"
            install_boost_args["cflags"] = [
                "-fPIC",
            ]
            install_boost_args["cxxflags"] = [
                "-fPIC",
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                "-std=gnu++17",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
                "-fexperimental-relative-c++-abi-vtables",
            ]
            install_boost_args["toolset"] = "clang"
            install_boost_args["android_ndk"] = os.path.join(install_dir, "android-ndk")
            install_boost_args["native_api_level"] = version["ANDROID_NATIVE_API_LEVEL"]
        elif platform.target.os == "jetson":
            sysroot = os.path.join(install_dir, "rootfs")
            install_boost_args["target_os"] = "linux"
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cflags"] = [
                "-fPIC",
                f"--sysroot={sysroot}",
                "--target=aarch64-linux-gnu",
                f"-I{os.path.join(sysroot, 'usr', 'include', 'aarch64-linux-gnu')}",
            ]
            install_boost_args["cxxflags"] = [
                "-fPIC",
                "--target=aarch64-linux-gnu",
                f"--sysroot={sysroot}",
                f"-I{os.path.join(sysroot, 'usr', 'include', 'aarch64-linux-gnu')}",
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                "-std=gnu++17",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
            ]
            install_boost_args["linkflags"] = [
                f"-L{os.path.join(sysroot, 'usr', 'lib', 'aarch64-linux-gnu')}",
                f"-B{os.path.join(sysroot, 'usr', 'lib', 'aarch64-linux-gnu')}",
            ]
            install_boost_args["toolset"] = "clang"
            install_boost_args["architecture"] = "arm"
        else:
            install_boost_args["target_os"] = "linux"
            install_boost_args["cxx"] = os.path.join(webrtc_info.clang_dir, "bin", "clang++")
            install_boost_args["cxxflags"] = [
                "-D_LIBCPP_ABI_NAMESPACE=Cr",
                "-D_LIBCPP_ABI_VERSION=2",
                "-D_LIBCPP_DISABLE_AVAILABILITY",
                "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                "-nostdinc++",
                f"-isystem{os.path.join(webrtc_info.libcxx_dir, 'include')}",
                "-fPIC",
            ]
            install_boost_args["toolset"] = "clang"

        build_and_install_boost(**install_boost_args)

        # CMake
        install_cmake_args = {
            "version": version["CMAKE_VERSION"],
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
        if platform.target.os == "windows":
            install_cuda_args = {
                "version": version["CUDA_VERSION"],
                "version_file": os.path.join(install_dir, "cuda.version"),
                "source_dir": source_dir,
                "build_dir": build_dir,
                "install_dir": install_dir,
            }
            install_cuda_windows(**install_cuda_args)

        # oneVPL
        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            install_vpl_args = {
                "version": version["VPL_VERSION"],
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
                cmake_args = []
                cmake_args.append("-DCMAKE_C_COMPILER=clang-18")
                cmake_args.append("-DCMAKE_CXX_COMPILER=clang++-18")
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

        # OpenH264
        install_openh264_args = {
            "version": version["OPENH264_VERSION"],
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
            libwebrtc = os.path.join(webrtc_info.webrtc_library_dir, "arm64-v8a", "libwebrtc.a")
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
            "version": version["BLEND2D_VERSION"] + "-" + version["ASMJIT_VERSION"],
            "version_file": os.path.join(install_dir, "blend2d.version"),
            "configuration": "Debug" if debug else "Release",
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "blend2d_version": version["BLEND2D_VERSION"],
            "asmjit_version": version["ASMJIT_VERSION"],
            "ios": platform.target.package_name == "ios",
            "cmake_args": [],
        }
        cmake_args = []
        if platform.target.os == "macos":
            sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
            target = (
                "x86_64-apple-darwin"
                if platform.target.arch == "x86_64"
                else "aarch64-apple-darwin"
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
        if platform.target.os == "ubuntu":
            if platform.target.package_name in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
                cmake_args.append("-DCMAKE_C_COMPILER=clang-18")
                cmake_args.append("-DCMAKE_CXX_COMPILER=clang++-18")
            else:
                cmake_args.append(
                    f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                )
                cmake_args.append(
                    f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                )
            path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
            cmake_args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
            cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
            cmake_args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
        if platform.target.os == "jetson":
            sysroot = os.path.join(install_dir, "rootfs")
            cmake_args.append("-DCMAKE_SYSTEM_NAME=Linux")
            cmake_args.append("-DCMAKE_SYSTEM_PROCESSOR=aarch64")
            cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
            cmake_args.append("-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu")
            cmake_args.append("-DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu")
            cmake_args.append(
                f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
            )
            cmake_args.append(
                f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
            )
            cmake_args.append(f"-DCMAKE_FIND_ROOT_PATH={sysroot}")
            cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER")
            cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH")
            cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH")
            cmake_args.append("-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH")
            path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
            cmake_args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
            cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
            cmake_args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
        if platform.target.os == "ios":
            cmake_args += ["-G", "Xcode"]
            cmake_args.append("-DCMAKE_SYSTEM_NAME=iOS")
            cmake_args.append("-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
            cmake_args.append(
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['IOS_DEPLOYMENT_TARGET']}"
            )
            cmake_args.append("-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO")
            cmake_args.append("-DBLEND2D_NO_JIT=ON")
        if platform.target.os == "android":
            toolchain_file = os.path.join(
                install_dir, "android-ndk", "build", "cmake", "android.toolchain.cmake"
            )
            android_native_api_level = version["ANDROID_NATIVE_API_LEVEL"]
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}")
            cmake_args.append(f"-DANDROID_NATIVE_API_LEVEL={android_native_api_level}")
            cmake_args.append(f"-DANDROID_PLATFORM={android_native_api_level}")
            cmake_args.append("-DANDROID_ABI=arm64-v8a")
            cmake_args.append("-DANDROID_STL=none")
            path = cmake_path(os.path.join(webrtc_info.libcxx_dir, "include"))
            cmake_args.append(f"-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES={path}")
            cmake_args.append("-DANDROID_CPP_FEATURES=exceptions rtti")
            # r23b には ANDROID_CPP_FEATURES=exceptions でも例外が設定されない問題がある
            # https://github.com/android/ndk/issues/1618
            cmake_args.append("-DCMAKE_ANDROID_EXCEPTIONS=ON")
            cmake_args.append("-DANDROID_NDK=OFF")
            cxxflags = ["-nostdinc++", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE"]
            cmake_args.append(f"-DCMAKE_CXX_FLAGS={' '.join(cxxflags)}")
        install_blend2d_args["cmake_args"] = cmake_args
        install_blend2d(**install_blend2d_args)


AVAILABLE_TARGETS = [
    "windows_x86_64",
    "macos_x86_64",
    "macos_arm64",
    "ubuntu-20.04_x86_64",
    "ubuntu-22.04_x86_64",
    "ubuntu-20.04_armv8_jetson",
    "ios",
    "android",
]
WINDOWS_SDK_VERSION = "10.0.20348.0"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=AVAILABLE_TARGETS)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--relwithdebinfo", action="store_true")
    add_webrtc_build_arguments(parser)
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--package", action="store_true")

    args = parser.parse_args()
    if args.target == "windows_x86_64":
        platform = Platform("windows", get_windows_osver(), "x86_64")
    elif args.target == "macos_x86_64":
        platform = Platform("macos", get_macos_osver(), "x86_64")
    elif args.target == "macos_arm64":
        platform = Platform("macos", get_macos_osver(), "arm64")
    elif args.target == "ubuntu-20.04_x86_64":
        platform = Platform("ubuntu", "20.04", "x86_64")
    elif args.target == "ubuntu-22.04_x86_64":
        platform = Platform("ubuntu", "22.04", "x86_64")
    elif args.target == "ubuntu-20.04_armv8_jetson":
        platform = Platform("jetson", None, "armv8")
    elif args.target == "ios":
        platform = Platform("ios", None, None)
    elif args.target == "android":
        platform = Platform("android", None, None)
    else:
        raise Exception(f"Unknown target {args.target}")

    logging.info(f"Build platform: {platform.build.package_name}")
    logging.info(f"Target platform: {platform.target.package_name}")

    configuration = "debug" if args.debug else "release"
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
        args.debug,
        webrtc_build_dir=args.webrtc_build_dir,
        webrtc_build_args=args.webrtc_build_args,
    )

    configuration = "Release"
    if args.debug:
        configuration = "Debug"
    if args.relwithdebinfo:
        configuration = "RelWithDebInfo"

    sora_build_dir = os.path.join(build_dir, "sora")
    mkdir_p(sora_build_dir)
    with cd(sora_build_dir):
        cmake_args = []
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        cmake_args.append(f"-DCMAKE_INSTALL_PREFIX={cmake_path(os.path.join(install_dir, 'sora'))}")
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(os.path.join(install_dir, 'boost'))}")
        webrtc_platform = get_webrtc_platform(platform)
        webrtc_info = get_webrtc_info(
            webrtc_platform, args.webrtc_build_dir, install_dir, args.debug
        )
        webrtc_version = read_version_file(webrtc_info.version_file)
        webrtc_deps = read_version_file(webrtc_info.deps_file)
        with cd(BASE_DIR):
            version = read_version_file("VERSION")
            sora_cpp_sdk_version = version["SORA_CPP_SDK_VERSION"]
            sora_cpp_sdk_commit = cmdcap(["git", "rev-parse", "HEAD"])
            android_native_api_level = version["ANDROID_NATIVE_API_LEVEL"]
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}")
        cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}")
        cmake_args.append(f"-DSORA_CPP_SDK_VERSION={sora_cpp_sdk_version}")
        cmake_args.append(f"-DSORA_CPP_SDK_COMMIT={sora_cpp_sdk_commit}")
        cmake_args.append(f"-DSORA_CPP_SDK_TARGET={platform.target.package_name}")
        cmake_args.append(f"-DWEBRTC_BUILD_VERSION={webrtc_version['WEBRTC_BUILD_VERSION']}")
        cmake_args.append(f"-DWEBRTC_READABLE_VERSION={webrtc_version['WEBRTC_READABLE_VERSION']}")
        cmake_args.append(f"-DWEBRTC_COMMIT={webrtc_version['WEBRTC_COMMIT']}")
        cmake_args.append(f"-DOPENH264_ROOT={cmake_path(os.path.join(install_dir, 'openh264'))}")
        if platform.target.os == "windows":
            cmake_args.append(f"-DCMAKE_SYSTEM_VERSION={WINDOWS_SDK_VERSION}")
        if platform.target.os == "ubuntu":
            if platform.target.package_name in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
                cmake_args.append("-DCMAKE_C_COMPILER=clang-18")
                cmake_args.append("-DCMAKE_CXX_COMPILER=clang++-18")
            else:
                cmake_args.append(
                    f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                )
                cmake_args.append(
                    f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                )
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
            cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={platform.target.arch}")
            cmake_args.append(f"-DCMAKE_OSX_ARCHITECTURES={platform.target.arch}")
            cmake_args.append(
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['MACOS_DEPLOYMENT_TARGET']}"
            )
            cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
        if platform.target.os == "ios":
            cmake_args += ["-G", "Xcode"]
            cmake_args.append("-DCMAKE_SYSTEM_NAME=iOS")
            cmake_args.append("-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
            cmake_args.append(
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={webrtc_deps['IOS_DEPLOYMENT_TARGET']}"
            )
            cmake_args.append("-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO")
        if platform.target.os == "android":
            toolchain_file = os.path.join(
                install_dir, "android-ndk", "build", "cmake", "android.toolchain.cmake"
            )
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}")
            cmake_args.append(f"-DANDROID_NATIVE_API_LEVEL={android_native_api_level}")
            cmake_args.append(f"-DANDROID_PLATFORM={android_native_api_level}")
            cmake_args.append("-DANDROID_ABI=arm64-v8a")
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
        if platform.target.os == "jetson":
            sysroot = os.path.join(install_dir, "rootfs")
            cmake_args.append("-DCMAKE_SYSTEM_NAME=Linux")
            cmake_args.append("-DCMAKE_SYSTEM_PROCESSOR=aarch64")
            cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
            cmake_args.append("-DCMAKE_C_COMPILER_TARGET=aarch64-linux-gnu")
            cmake_args.append("-DCMAKE_CXX_COMPILER_TARGET=aarch64-linux-gnu")
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
            cmake_args.append("-DUSE_JETSON_ENCODER=ON")

        # NvCodec
        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            cmake_args.append("-DUSE_NVCODEC_ENCODER=ON")
            if platform.target.os == "windows":
                cmake_args.append(
                    f"-DCUDA_TOOLKIT_ROOT_DIR={cmake_path(os.path.join(install_dir, 'cuda'))}"
                )

        if platform.target.os in ("windows", "ubuntu") and platform.target.arch == "x86_64":
            cmake_args.append("-DUSE_VPL_ENCODER=ON")
            cmake_args.append(f"-DVPL_ROOT_DIR={cmake_path(os.path.join(install_dir, 'vpl'))}")

        # バンドルされたライブラリを消しておく
        # （CMake でうまく依存関係を解消できなくて更新されないため）
        rm_rf(os.path.join(sora_build_dir, "bundled"))
        rm_rf(os.path.join(sora_build_dir, "libsora.a"))

        cmd(["cmake", BASE_DIR] + cmake_args)
        if platform.target.os == "ios":
            cmd(
                [
                    "cmake",
                    "--build",
                    ".",
                    f"-j{multiprocessing.cpu_count()}",
                    "--config",
                    configuration,
                    "--target",
                    "sora",
                    "--",
                    "-arch",
                    "arm64",
                    "-sdk",
                    "iphoneos",
                ]
            )
            cmd(["cmake", "--install", "."])
        else:
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
        elif platform.target.os == "ubuntu":
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

    if args.test:
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
                cmd(["./gradlew", "--no-daemon", "assemble"])
        else:
            # 普通のプロジェクトは CMake でビルドする
            test_build_dir = os.path.join(build_dir, "test")
            mkdir_p(test_build_dir)
            with cd(test_build_dir):
                cmake_args = []
                cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
                cmake_args.append(f"-DBOOST_ROOT={cmake_path(os.path.join(install_dir, 'boost'))}")
                cmake_args.append(
                    f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}"
                )
                cmake_args.append(
                    f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}"
                )
                cmake_args.append(f"-DSORA_DIR={cmake_path(os.path.join(install_dir, 'sora'))}")
                cmake_args.append(
                    f"-DBLEND2D_ROOT_DIR={cmake_path(os.path.join(install_dir, 'blend2d'))}"
                )
                if platform.target.os == "macos":
                    sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
                    target = (
                        "x86_64-apple-darwin"
                        if platform.target.arch == "x86_64"
                        else "aarch64-apple-darwin"
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
                if platform.target.os == "ubuntu":
                    if platform.target.package_name in (
                        "ubuntu-20.04_x86_64",
                        "ubuntu-22.04_x86_64",
                    ):
                        cmake_args.append("-DCMAKE_C_COMPILER=clang-18")
                        cmake_args.append("-DCMAKE_CXX_COMPILER=clang++-18")
                    else:
                        cmake_args.append(
                            f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang'))}"
                        )
                        cmake_args.append(
                            f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(webrtc_info.clang_dir, 'bin', 'clang++'))}"
                        )
                    cmake_args.append("-DUSE_LIBCXX=ON")
                    cmake_args.append(
                        f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(webrtc_info.libcxx_dir, 'include'))}"
                    )
                if platform.target.os == "jetson":
                    sysroot = os.path.join(install_dir, "rootfs")
                    cmake_args.append("-DJETSON=ON")
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

                if args.run:
                    if platform.target.os == "windows":
                        cmd(
                            [
                                os.path.join(test_build_dir, configuration, "hello.exe"),
                                os.path.join(BASE_DIR, "test", ".testparam.json"),
                            ]
                        )
                    else:
                        cmd(
                            [
                                os.path.join(test_build_dir, "hello"),
                                os.path.join(BASE_DIR, "test", ".testparam.json"),
                            ]
                        )

    if args.package:
        mkdir_p(package_dir)
        rm_rf(os.path.join(package_dir, "sora"))
        rm_rf(os.path.join(package_dir, "sora.env"))

        with cd(BASE_DIR):
            version = read_version_file("VERSION")
            sora_cpp_sdk_version = version["SORA_CPP_SDK_VERSION"]
            boost_version = version["BOOST_VERSION"]

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


if __name__ == "__main__":
    main()
