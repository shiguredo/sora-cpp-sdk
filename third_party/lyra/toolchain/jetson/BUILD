load("toolchain_config.bzl", "aarch64_cc_toolchain_config")

aarch64_cc_toolchain_config(
    name = "aarch64_toolchain_config",
)

filegroup(
    name = "empty",
    srcs = []
)

cc_toolchain(
    name = "cc_compiler_linux_aarch64_gcc",
    all_files = ":empty",
    ar_files = ":empty",
    as_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 0,
    toolchain_config = ":aarch64_toolchain_config",
    toolchain_identifier = "local_aarch64",
)

cc_toolchain_suite(
    name = "toolchain",
    toolchains = {
        "aarch64": ":cc_compiler_linux_aarch64_gcc",
    },
)

#toolchain(
#    name = "cc_toolchain_linux_aarch64",
#    toolchain = ":cc_compiler_linux_aarch64_gcc",
#    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
#    target_compatible_with = [
#        "@platforms//os:linux",
#        "@platforms//cpu:aarch64",
#    ],
#)
