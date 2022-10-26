cc_binary(
    name = "lyra",
    linkshared = 1,
    srcs = [
        "lyra.cc",
    ],
    defines = [
        "-DLYRA_EXPORTS",
    ],
    deps = [
        "@lyra//:lyra_encoder",
        "@lyra//:lyra_decoder",
    ],
)
