load("@bazel_tools//tools/cpp:unix_cc_toolchain_config.bzl", "cc_toolchain_config")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "empty",
    srcs = []
)

cc_toolchain_config(
    name = "android_arm64_v8a_toolchain_config",
    cpu = "arm64-v8a",
    compiler = "clang",
    toolchain_identifier = "android_arm64_v8a_toolchain",
    host_system_name = "Android arm64-v8a",
    target_system_name = "Android arm64-v8a",
    target_libc = "libc",
    abi_version = "local",
    abi_libc_version = "local",
    cxx_builtin_include_directories = [
        '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/%{clang_version}/include',
        '%sysroot%/usr/include',
        '%sysroot%/usr/local/include',
        "%{webrtc_include_dir}/third_party/libc++abi/src/include",
        "%{llvm_dir}/libcxx/include",
    ],
    tool_paths = {
        "ar": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar',
        "cpp": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++',
        "gcc": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang',
        "ld": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/lld',
        "nm": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm',
        "strip": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip',
        "llvm-cov": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/llvm-cov',
        "objdump": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump',
        "objcopy": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objcopy',
        "dwp": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-dwp',
        "llvm-profdata": '%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/llvm-profdata',
    },
    compile_flags = [
        "-fstack-protector",
        # All warnings are enabled.
        "-Wall",
        # Enable a few more warnings that aren't part of -Wall.
        "-Wthread-safety",
        "-Wself-assign",
        # Disable problematic warnings.
        #"-Wunused-but-set-parameter",
        # has false positives
        "-Wno-free-nonheap-object",
        # Enable coloring even if there's no attached terminal. Bazel removes the
        # escape sequences if --nocolor is specified.
        "-fcolor-diagnostics",
        # Keep stack frames for debugging, even in opt mode.
        "-fno-omit-frame-pointer",

        "--target=aarch64-none-linux-android%{android_api}",
        "-D__ANDROID_API__=%{android_api}",
    ],
    dbg_compile_flags = [
        "-g",
    ],
    opt_compile_flags = [
        # No debug symbols.
        # Maybe we should enable https://gcc.gnu.org/wiki/DebugFission for opt or
        # even generally? However, that can't happen here, as it requires special
        # handling in Bazel.
        "-g0",

        # Conservative choice for -O
        # -O3 can increase binary size and even slow down the resulting binaries.
        # Profile first and / or use FDO if you need better performance than this.
        "-O2",

        # Security hardening on by default.
        # Conservative choice; -D_FORTIFY_SOURCE=2 may be unsafe in some cases.
        "-D_FORTIFY_SOURCE=1",

        # Disable assertions
        "-DNDEBUG",

        # Removal of unused code and data at link time (can this increase binary
        # size in some cases?).
        "-ffunction-sections",
        "-fdata-sections",
    ],
    # conly_flags = [],
    cxx_flags = [
        "-isystem%{llvm_dir}/libcxx/include",
        "-isystem%{webrtc_include_dir}/third_party/libc++abi/src/include",
        "-std=c++17",
        "-nostdinc++",
        "-D_LIBCPP_ABI_NAMESPACE=Cr",
        "-D_LIBCPP_ABI_VERSION=2",
        "-D_LIBCPP_DISABLE_AVAILABILITY",
        "-fexperimental-relative-c++-abi-vtables",
        "-fexceptions",
    ],
    link_flags = [
        "-Wl,-no-as-needed",
        "-Wl,-z,relro,-z,now",
        "-B", "%{llvm_dir}/clang/bin/",
        "-L", "%{webrtc_library_dir}",
        "--target=aarch64-none-linux-android%{android_api}",
    ],
    # archive_flags = [],
    link_libs = [
        "-lwebrtc",
        "-lpthread",
        "-lm",
    ],
    opt_link_flags = [
        "-Wl,--gc-sections",
    ],
    unfiltered_compile_flags = [],
    coverage_compile_flags = [],
    coverage_link_flags = [],
    supports_start_end_lib = False,
    builtin_sysroot = "%{android_ndk}/toolchains/llvm/prebuilt/linux-x86_64/sysroot",
)

cc_toolchain(
    name = "cc_compiler_android_arm64_v8a_clang",
    all_files = ":empty",
    ar_files = ":empty",
    as_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
    toolchain_config = ":android_arm64_v8a_toolchain_config",
    toolchain_identifier = "local_android_arm64_v8a",
)

cc_toolchain_suite(
    name = "toolchain",
    toolchains = {
        "arm64-v8a": ":cc_compiler_android_arm64_v8a_clang",
    },
)

config_setting(
    name = "android_arm64",
    values = {
        "crosstool_top": "@android_toolchain//:toolchain",
        "cpu": "arm64-v8a",
    },
    visibility = ["//visibility:public"],
)
