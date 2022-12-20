def _impl(repository_ctx):
    llvm_dir = repository_ctx.os.environ['BAZEL_LLVM_DIR']
    webrtc_include_dir = repository_ctx.os.environ['BAZEL_WEBRTC_INCLUDE_DIR']
    webrtc_library_dir = repository_ctx.os.environ['BAZEL_WEBRTC_LIBRARY_DIR']
    repository_ctx.template(
        "BUILD",
        repository_ctx.attr.src,
        {
            "%{llvm_dir}": llvm_dir,
            "%{webrtc_include_dir}": webrtc_include_dir,
            "%{webrtc_library_dir}": webrtc_library_dir,
        },
        False
    )


webrtc_clang_toolchain_configure = repository_rule(
    implementation = _impl,
    attrs = {
        "src": attr.label(executable = False, mandatory = True),
    }
)
