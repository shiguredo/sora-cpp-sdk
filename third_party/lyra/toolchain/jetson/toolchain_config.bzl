load("@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl", "tool_path", "feature")

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(out, "AArch64 executable")
    tool_paths = [              
        tool_path(
            name = "ar",
            path = "/usr/bin/aarch64-linux-gnu-ar",
        ),
        tool_path(
            name = "cpp",
            path = "/usr/bin/aarch64-linux-gnu-cpp",
        ),
        tool_path(
            name = "gcc",
            path = "/usr/bin/aarch64-linux-gnu-gcc",
        ),
        tool_path(
            name = "gcov",
            path = "/usr/bin/aarch64-linux-gnu-gcov",
        ),
        tool_path(
            name = "ld",
            path = "/usr/bin/aarch64-linux-gnu-ld",
        ),
        tool_path(
            name = "nm",
            path = "/usr/bin/aarch64-linux-gnu-nm",
        ),  
        tool_path(
            name = "objdump",
            path = "/usr/bin/aarch64-linux-gnu-objdump",
        ),
        tool_path(
            name = "strip",
            path = "/usr/bin/aarch64-linux-gnu-strip",
        ),
    ]

    opt_feature = feature(name = "opt")
    dbg_feature = feature(name = "dbg")

    features = [
        opt_feature,
        dbg_feature,
    ]
    action_configs = []
    cxx_builtin_include_directories = [
        "/usr/aarch64-linux-gnu/include",
        "/usr/aarch64-linux-gnu/include/c++/9",
        "/usr/lib/gcc-cross/aarch64-linux-gnu/9/include",
        "/usr/include",
    ]

    return [
        cc_common.create_cc_toolchain_config_info(
            ctx = ctx,
            features = features,
            action_configs = action_configs,
            cxx_builtin_include_directories = cxx_builtin_include_directories,
            toolchain_identifier = "aarch64-linux-gnu-toolchain",
            host_system_name = "Ubuntu x86_64",
            target_system_name = "Jetson",
            target_cpu = "aarch64",
            target_libc = "libc",
            cc_target_os = "Jetson",
            compiler = "gcc",
            abi_version = "nothing",
            abi_libc_version = "nothing",
            tool_paths = tool_paths,
        ),
        DefaultInfo(
            executable = out,
        ),
    ]

aarch64_cc_toolchain_config = rule(
    implementation = _impl,
    provides = [CcToolchainConfigInfo],
    executable = True,
)
