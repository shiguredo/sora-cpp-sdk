def _impl(repository_ctx):
    if not ('ANDROID_NDK_HOME' in repository_ctx.os.environ and
        'ANDROID_API' in repository_ctx.os.environ):
        return

    if not ('CLANG_VERSION' in repository_ctx.os.environ and
        'BAZEL_CLANG_DIR' in repository_ctx.os.environ and
        'BAZEL_LIBCXX_DIR' in repository_ctx.os.environ and
        'BAZEL_WEBRTC_INCLUDE_DIR' in repository_ctx.os.environ and
        'BAZEL_WEBRTC_LIBRARY_DIR' in repository_ctx.os.environ):
        return

    android_ndk = repository_ctx.os.environ['ANDROID_NDK_HOME']
    android_api = repository_ctx.os.environ['ANDROID_API']
    clang_version = repository_ctx.os.environ['CLANG_VERSION']
    clang_dir = repository_ctx.os.environ['BAZEL_CLANG_DIR']
    libcxx_dir = repository_ctx.os.environ['BAZEL_LIBCXX_DIR']
    webrtc_include_dir = repository_ctx.os.environ['BAZEL_WEBRTC_INCLUDE_DIR']
    webrtc_library_dir = repository_ctx.os.environ['BAZEL_WEBRTC_LIBRARY_DIR']
    repository_ctx.template(
        "BUILD",
        repository_ctx.attr.src,
        {
            "%{android_ndk}": android_ndk,
            "%{android_api}": android_api,
            "%{clang_version}": clang_version,
            "%{clang_dir}": clang_dir,
            "%{libcxx_dir}": libcxx_dir,
            "%{webrtc_include_dir}": webrtc_include_dir,
            "%{webrtc_library_dir}": webrtc_library_dir,
        },
        False
    )


android_toolchain_configure = repository_rule(
    implementation = _impl,
    attrs = {
        "src": attr.label(executable = False, mandatory = True),
    }
)
