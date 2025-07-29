load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "grpc_status_metrics_filter_lib",
    srcs = ["grpc_status_metrics_filter.cc"],
    hdrs = ["grpc_status_metrics_filter.h"],
    deps = [
        "//envoy/grpc:status_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:logger_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:status_lib",
        "//source/common/grpc:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_status_metrics/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["grpc_status_metrics_filter.cc"],
    hdrs = ["grpc_status_metrics_filter.h"],
    category = "envoy.filters.http",
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":grpc_status_metrics_filter_lib",
    ],
)