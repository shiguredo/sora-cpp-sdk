def _impl(repository_ctx):
    if not ('CLANG_VERSION' in repository_ctx.os.environ and
        'BAZEL_CLANG_DIR' in repository_ctx.os.environ and
        'BAZEL_LIBCXX_DIR' in repository_ctx.os.environ and
        'BAZEL_WEBRTC_INCLUDE_DIR' in repository_ctx.os.environ and
        'BAZEL_WEBRTC_LIBRARY_DIR' in repository_ctx.os.environ):
        return

    clang_version = repository_ctx.os.environ['CLANG_VERSION']
    clang_dir = repository_ctx.os.environ['BAZEL_CLANG_DIR']
    llvm_postfix = repository_ctx.os.environ['BAZEL_LLVM_POSTFIX'] if 'BAZEL_LLVM_POSTFIX' in repository_ctx.os.environ else ''
    libcxx_dir = repository_ctx.os.environ['BAZEL_LIBCXX_DIR']
    webrtc_include_dir = repository_ctx.os.environ['BAZEL_WEBRTC_INCLUDE_DIR']
    webrtc_library_dir = repository_ctx.os.environ['BAZEL_WEBRTC_LIBRARY_DIR']
    sysroot = repository_ctx.os.environ['BAZEL_SYSROOT'] if 'BAZEL_SYSROOT' in repository_ctx.os.environ else ''
    repository_ctx.template(
        "BUILD",
        repository_ctx.attr.src,
        {
            "%{clang_version}": clang_version,
            "%{clang_dir}": clang_dir,
            "%{llvm_postfix}": llvm_postfix,
            "%{libcxx_dir}": libcxx_dir,
            "%{webrtc_include_dir}": webrtc_include_dir,
            "%{webrtc_library_dir}": webrtc_library_dir,
            "%{sysroot}": sysroot,
        },
        False
    )


webrtc_clang_toolchain_configure = repository_rule(
    implementation = _impl,
    attrs = {
        "src": attr.label(executable = False, mandatory = True),
    }
)
