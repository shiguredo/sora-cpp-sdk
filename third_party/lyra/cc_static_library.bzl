load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

def _cc_static_library_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    windows_constraint = ctx.attr._windows_constraint[platform_common.ConstraintValueInfo]
    macos_constraint = ctx.attr._macos_constraint[platform_common.ConstraintValueInfo]

    if ctx.target_platform_has_constraint(windows_constraint):
        output = ctx.actions.declare_file("{}.lib".format(ctx.attr.name))
    else:
        output = ctx.actions.declare_file("lib{}.a".format(ctx.attr.name))

    lib_sets = []
    for dep in ctx.attr.deps:
        lib_sets.append(dep[CcInfo].linking_context.linker_inputs)
    input_depset = depset(transitive = lib_sets)

    libs = []
    # dep: LinkerInput
    for dep in input_depset.to_list():
        # lib: LibraryToLink
        for lib in dep.libraries:
            if lib.pic_static_library != None:
                libs.append(lib.pic_static_library)
            elif lib.static_library != None:
                libs.append(lib.static_library)

    lib_paths = [lib.path for lib in libs]

    # WebRTC とコンフリクトしてるやつは除ける（WebRTC 側を優先する）
    #lib_paths = [path for path in lib_paths if 'protobuf.lib' not in path]

    ar_path = cc_toolchain.ar_executable

    if ctx.target_platform_has_constraint(windows_constraint):
        command = "\"{0}\" /OUT:{1} {2}".format(ar_path, output.path, " ".join(lib_paths))
    elif ctx.target_platform_has_constraint(macos_constraint):
        command = '"{0}" -static -o {1} {2}'.format('libtool', output.path, " ".join(lib_paths))
    else:
        command = 'echo "CREATE {1}\n{2}\nSAVE\nEND\n" | "{0}" -M'.format(ar_path, output.path, "\n".join(["ADDLIB " + path for path in lib_paths]))

    print(command)

    ctx.actions.run_shell(
        command = command,
        inputs = libs + cc_toolchain.all_files.to_list(),
        outputs = [output],
        mnemonic = "Archive",
        progress_message = "Archiving all files to {}".format(output.path),
    )
    return [
        DefaultInfo(files = depset([output])),
    ]

cc_static_library = rule(
    implementation = _cc_static_library_impl,
    attrs = {
        "deps": attr.label_list(),
        "_cc_toolchain": attr.label(
            default = "@bazel_tools//tools/cpp:current_cc_toolchain",
        ),
        '_windows_constraint': attr.label(default = "@platforms//os:windows"),
        '_macos_constraint': attr.label(default = "@platforms//os:macos"),
    },
    toolchains = use_cpp_toolchain(),
    incompatible_use_toolchain_transition = True,
)