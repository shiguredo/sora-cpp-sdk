load("@bazel_tools//tools/cpp:unix_cc_toolchain_config.bzl", "cc_toolchain_config")

filegroup(
    name = "empty",
    srcs = []
)

# cc_toolchain_config は
# https://github.com/bazelbuild/bazel/blob/7b88517ba64434821d388e2490fa3fee2bb95437/tools/cpp/unix_cc_configure.bzl
# の実装を参考にして記述

cc_toolchain_config(
    name = "linux_x86_64_toolchain_config",
    cpu = "x86_64",
    compiler = "clang",
    toolchain_identifier = "linux_x86_64_toolchain",
    host_system_name = "Linux x86_64",
    target_system_name = "Linux x86_64",
    target_libc = "libc",
    abi_version = "local",
    abi_libc_version = "local",
    cxx_builtin_include_directories = [
        "%{clang_dir}/lib/clang/%{clang_version}/include",
        "/usr/include/x86_64-linux-gnu",
        "/usr/include",
        "%{libcxx_dir}/include",
        "%{webrtc_include_dir}/third_party/libc++abi/src/include",
    ],
    tool_paths = {
        "ar": "%{clang_dir}/bin/llvm-ar%{llvm_postfix}",
        "cpp": "%{clang_dir}/bin/clang%{llvm_postfix}++",
        "gcc": "%{clang_dir}/bin/clang%{llvm_postfix}",
        "ld": "%{clang_dir}/bin/lld%{llvm_postfix}",
        "nm": "%{clang_dir}/bin/llvm-nm%{llvm_postfix}",
        "strip": "%{clang_dir}/bin/llvm-strip%{llvm_postfix}",
        "llvm-cov": "llvm-cov",
        "objdump": "objdump",
    },
    compile_flags = [
        "-no-canonical-prefixes",
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
        "-isystem%{libcxx_dir}/include",
        "-isystem%{webrtc_include_dir}/third_party/libc++abi/src/include",
        "-std=c++17",
        "-nostdinc++",
        "-D_LIBCPP_ABI_NAMESPACE=Cr",
        "-D_LIBCPP_ABI_VERSION=2",
        "-D_LIBCPP_DISABLE_AVAILABILITY",
        '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE',
    ],
    link_flags = [
        "-Wl,-no-as-needed",
        "-Wl,-z,relro,-z,now",
        "-B", "%{clang_dir}/bin/",
        "-L", "%{webrtc_library_dir}",
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
    builtin_sysroot = "",
)

cc_toolchain(
    name = "cc_compiler_linux_x86_64_clang",
    all_files = ":empty",
    ar_files = ":empty",
    as_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
    toolchain_config = ":linux_x86_64_toolchain_config",
    toolchain_identifier = "local_linux_x86_64",
)

cc_toolchain_config(
    name = "linux_aarch64_toolchain_config",
    cpu = "aarch64",
    compiler = "clang",
    toolchain_identifier = "linux_aarch64_toolchain",
    host_system_name = "Linux aarch64",
    target_system_name = "Linux aarch64",
    target_libc = "libc",
    abi_version = "local",
    abi_libc_version = "local",
    cxx_builtin_include_directories = [
        "%{clang_dir}/lib/clang/%{clang_version}/include",
        "%{sysroot}/usr/include/aarch64-linux-gnu",
        "%{sysroot}/usr/include",
        "/usr/include/aarch64-linux-gnu",
        "/usr/include",
        "%{libcxx_dir}/include",
        "%{webrtc_include_dir}/third_party/libc++abi/src/include",
    ],
    tool_paths = {
        "ar": "%{clang_dir}/bin/llvm-ar%{llvm_postfix}",
        "cpp": "%{clang_dir}/bin/clang%{llvm_postfix}++",
        "gcc": "%{clang_dir}/bin/clang%{llvm_postfix}",
        "ld": "%{clang_dir}/bin/lld%{llvm_postfix}",
        "nm": "%{clang_dir}/bin/llvm-nm%{llvm_postfix}",
        "strip": "%{clang_dir}/bin/llvm-strip%{llvm_postfix}",
        "llvm-cov": "llvm-cov",
        "objdump": "objdump",
    },
    compile_flags = [
        "-no-canonical-prefixes",
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

        "--target=aarch64-linux-gnu",
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
        "-isystem%{libcxx_dir}/include",
        "-isystem%{webrtc_include_dir}/third_party/libc++abi/src/include",
        "-std=c++17",
        "-nostdinc++",
        "-D_LIBCPP_ABI_NAMESPACE=Cr",
        "-D_LIBCPP_ABI_VERSION=2",
        "-D_LIBCPP_DISABLE_AVAILABILITY",
        '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE',
    ],
    link_flags = [
        "-Wl,-no-as-needed",
        "-Wl,-z,relro,-z,now",
        "-B", "%{clang_dir}/bin/",
        "-L", "%{webrtc_library_dir}",
        "--target=aarch64-linux-gnu",
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
    builtin_sysroot = "%{sysroot}",
)

cc_toolchain(
    name = "cc_compiler_linux_aarch64_clang",
    all_files = ":empty",
    ar_files = ":empty",
    as_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
    toolchain_config = ":linux_aarch64_toolchain_config",
    toolchain_identifier = "local_linux_aarch64",
)

cc_toolchain_suite(
    name = "toolchain",
    toolchains = {
        "k8": ":cc_compiler_linux_x86_64_clang",
        "aarch64": ":cc_compiler_linux_aarch64_clang",
    },
)
