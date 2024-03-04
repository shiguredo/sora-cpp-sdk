load("cc_static_library.bzl", "cc_static_library")

cc_library(
    name = "lyrac",
    srcs = [
        "lyra.h",
        "lyra.cc",
    ],
    defines = [
        # "LYRA_EXPORTS",
    ],
    deps = [
        "@lyra//lyra:lyra_encoder",
        "@lyra//lyra:lyra_decoder",
    ],
)

cc_static_library(
    name = "lyra",
    deps = [
        ":lyrac",
    ],
)
