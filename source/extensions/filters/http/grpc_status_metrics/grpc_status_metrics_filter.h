#pragma once

#include "envoy/extensions/filters/http/grpc_status_metrics/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_status_metrics/v3/config.pb.validate.h"
#include "envoy/grpc/status.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/metadata.h"

#include "source/common/common/logger.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStatusMetrics {

/**
 * All gRPC status metrics filter stats. @see stats_macros.h
 */
#define ALL_GRPC_STATUS_METRICS_FILTER_STATS(COUNTER)                                             \
  COUNTER(grpc_requests_total)                                                                     \
  COUNTER(grpc_status_0)                                                                           \
  COUNTER(grpc_status_1)                                                                           \
  COUNTER(grpc_status_2)                                                                           \
  COUNTER(grpc_status_3)                                                                           \
  COUNTER(grpc_status_4)                                                                           \
  COUNTER(grpc_status_5)                                                                           \
  COUNTER(grpc_status_6)                                                                           \
  COUNTER(grpc_status_7)                                                                           \
  COUNTER(grpc_status_8)                                                                           \
  COUNTER(grpc_status_9)                                                                           \
  COUNTER(grpc_status_10)                                                                          \
  COUNTER(grpc_status_11)                                                                          \
  COUNTER(grpc_status_12)                                                                          \
  COUNTER(grpc_status_13)                                                                          \
  COUNTER(grpc_status_14)                                                                          \
  COUNTER(grpc_status_15)                                                                          \
  COUNTER(grpc_status_16)                                                                          \
  COUNTER(grpc_status_unknown)

struct GrpcStatusMetricsFilterStats {
  ALL_GRPC_STATUS_METRICS_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

struct Config {
  Config(const envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig& proto_config,
         Server::Configuration::FactoryContext& context);

  const std::string metric_name_prefix_;
  const bool emit_success_metrics_;
  const bool include_http_status_;
  const bool include_service_method_;
  Stats::Scope& scope_;
  GrpcStatusMetricsFilterStats stats_;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class GrpcStatusMetricsFilter : public Http::PassThroughFilter,
                                Logger::Loggable<Logger::Id::filter> {
public:
  GrpcStatusMetricsFilter(ConfigConstSharedPtr config);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;

  // Http::StreamDecoderFilterCallbacks
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

private:
  void recordGrpcStatusMetric(Grpc::Status::GrpcStatus status_code, 
                             absl::optional<uint64_t> http_status = absl::nullopt,
                             const std::string& deployment = "");
  void extractAndRecordStatus(const Http::ResponseHeaderOrTrailerMap& headers_or_trailers,
                             absl::optional<uint64_t> http_status = absl::nullopt);
  
  // Extract deployment name from upstream host LB metadata
  absl::optional<std::string> extractDeploymentFromUpstream();
  
  // LB metadata extraction helper
  absl::optional<std::string> extractLbMetadataValue(
      const Upstream::MetadataConstSharedPtr& upstream_host_metadata,
      const std::string& key_name);

  // Dynamic stats helpers
  void incCounter(Stats::Scope& scope, const Stats::StatName& stat);
  void incGauge(Stats::Scope& scope, const Stats::StatName& stat);
  void setGauge(Stats::Scope& scope, const Stats::StatName& stat, uint64_t value);

  ConfigConstSharedPtr config_;
  bool is_grpc_request_;
  absl::optional<Grpc::Common::RequestNames> request_names_;
  absl::optional<std::string> deployment_name_;
  Stats::StatNamePool dynamic_pool_;
  
  // Constants for LB metadata
  static constexpr absl::string_view LBMetadataName = "envoy.lb";
  static constexpr absl::string_view DeploymentMetadataKey = "deployment";
};

class GrpcStatusMetricsFilterConfigFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig> {
public:
  GrpcStatusMetricsFilterConfigFactory() : FactoryBase("envoy.filters.http.grpc_status_metrics") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace GrpcStatusMetrics
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy