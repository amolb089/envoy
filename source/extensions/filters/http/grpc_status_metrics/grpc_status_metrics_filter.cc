#include "source/extensions/filters/http/grpc_status_metrics/grpc_status_metrics_filter.h"

#include "envoy/grpc/status.h"
#include "envoy/registry/registry.h"

#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/grpc/utility.h"
#include "source/common/http/utility.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStatusMetrics {

Config::Config(const envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig& proto_config,
               Server::Configuration::FactoryContext& context)
    : metric_name_prefix_(proto_config.metric_name_prefix().empty() ? "grpc_status" : proto_config.metric_name_prefix()),
      emit_success_metrics_(proto_config.emit_success_metrics()),
      include_http_status_(proto_config.include_http_status()),
      include_service_method_(proto_config.include_service_method()),
      scope_(context.scope()),
      stats_(GrpcStatusMetricsFilterStats{
          ALL_GRPC_STATUS_METRICS_FILTER_STATS(POOL_COUNTER_PREFIX(scope_, metric_name_prefix_))}) {}

IRMetricFilter::IRMetricFilter(ConfigConstSharedPtr config)
    : config_(config), is_grpc_request_(false), dynamic_pool_(config->scope_.symbolTable()) {}

Http::FilterHeadersStatus IRMetricFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                       bool) {
  // Check if this is a gRPC request
  is_grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);
  
  if (is_grpc_request_) {
    ENVOY_LOG(debug, "Detected gRPC request");
    
    // Optionally extract service and method names for more granular metrics
    if (config_->include_service_method_) {
      request_names_ = Grpc::Common::resolveServiceAndMethod(headers.Path());
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterHeadersStatus IRMetricFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                       bool end_stream) {
  // Only process if this was detected as a gRPC request AND has a gRPC response
  // Check if this is a gRPC response
  bool is_grpc_response = Grpc::Common::isGrpcResponseHeaders(headers, end_stream);
  
  if (is_grpc_request_ && is_grpc_response) {
    ENVOY_LOG(debug, "Detected gRPC request with gRPC response");
    
    // Extract deployment name from upstream metadata for dimension
    deployment_name_ = extractDeploymentFromUpstream();
    if (deployment_name_.has_value()) {
      ENVOY_LOG(debug, "Found deployment: {}", deployment_name_.value());
    }
    
    // Always extract HTTP status code for metrics
    absl::optional<uint64_t> http_status = Http::Utility::getResponseStatus(headers);
    
    // Try to extract gRPC status from headers
    extractAndRecordStatus(headers, http_status);
  } else {
    ENVOY_LOG(debug, "Not a gRPC request+response pair (request: {}, response: {})", 
              is_grpc_request_, is_grpc_response);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus IRMetricFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  if (!is_grpc_request_) {
    return Http::FilterTrailersStatus::Continue;
  }

  ENVOY_LOG(debug, "Processing gRPC response trailers");
  
  // Extract deployment name if not already extracted
  if (!deployment_name_.has_value()) {
    deployment_name_ = extractDeploymentFromUpstream();
    if (deployment_name_.has_value()) {
      ENVOY_LOG(debug, "Found deployment in trailers: {}", deployment_name_.value());
    }
  }
  
  // Extract gRPC status from trailers (this is the most common location)
  extractAndRecordStatus(trailers);

  return Http::FilterTrailersStatus::Continue;
}

void IRMetricFilter::extractAndRecordStatus(
    const Http::ResponseHeaderOrTrailerMap& headers_or_trailers,
    absl::optional<uint64_t> http_status) {
  
  // Try to get the gRPC status code
  absl::optional<Grpc::Status::GrpcStatus> status = 
      Grpc::Common::getGrpcStatus(headers_or_trailers, true /* allow_user_defined */);
  
  std::string deployment = deployment_name_.value_or("");
  
  if (status.has_value()) {
    ENVOY_LOG(debug, "Found gRPC status: {}", static_cast<uint64_t>(status.value()));
    recordGrpcStatusMetric(status.value(), http_status, deployment);
  } else {
    ENVOY_LOG(debug, "No gRPC status found in headers/trailers");
    // If no gRPC status is found but we know this is a gRPC request,
    // we might want to infer it from HTTP status or treat it as unknown
    if (http_status.has_value()) {
      // Convert HTTP status to gRPC status using utility function
      Grpc::Status::GrpcStatus inferred_status = Grpc::Utility::httpToGrpcStatus(http_status.value());
      recordGrpcStatusMetric(inferred_status, http_status, deployment);
    } else {
      // Record as unknown status
      recordGrpcStatusMetric(Grpc::Status::WellKnownGrpcStatus::Unknown, absl::nullopt, deployment);
    }
  }
}

void IRMetricFilter::recordGrpcStatusMetric(Grpc::Status::GrpcStatus status_code,
                                            absl::optional<uint64_t> http_status,
                                            const std::string& deployment) {
  
  // Check if we should emit success metrics
  if (!config_->emit_success_metrics_ && status_code == Grpc::Status::WellKnownGrpcStatus::Ok) {
    return;
  }

  std::string http_status_str = http_status.has_value() ? std::to_string(http_status.value()) : "unknown";
  
  ENVOY_LOG(debug, "Recording gRPC status metric: {} (HTTP: {}, Deployment: {})", 
            static_cast<uint64_t>(status_code),
            http_status_str,
            deployment.empty() ? "unknown" : deployment);

  // Increment total gRPC requests counter (static metric)
  config_->stats_.grpc_requests_total_.inc();

  // Build dynamic metric name with deployment dimension
  std::string status_name;
  uint64_t status_num = static_cast<uint64_t>(status_code);
  
  if (status_num <= 16) {
    status_name = absl::StrCat("grpc_status_", status_num);
  } else {
    status_name = "grpc_status_unknown";
  }

  // Create base metric components
  std::string deployment_part = deployment.empty() ? "unknown_deployment" : deployment;
  
  // 1. Create metric with deployment and gRPC status only
  std::string grpc_metric_name = absl::StrCat(
      config_->metric_name_prefix_, ".", deployment_part, ".", status_name);
  
  const auto& grpc_stat_name = dynamic_pool_.add(grpc_metric_name);
  incCounter(config_->scope_, grpc_stat_name);
  ENVOY_LOG(debug, "Recorded gRPC metric: {}", grpc_metric_name);

  // 2. Create metric with deployment, HTTP status, and gRPC status
  std::string combined_metric_name = absl::StrCat(
      config_->metric_name_prefix_, ".", deployment_part, ".http_", http_status_str, ".", status_name);
  
  const auto& combined_stat_name = dynamic_pool_.add(combined_metric_name);
  incCounter(config_->scope_, combined_stat_name);
  ENVOY_LOG(debug, "Recorded combined HTTP+gRPC metric: {}", combined_metric_name);

  // 3. If HTTP status correlation is enabled, create HTTP-only metrics
  if (config_->include_http_status_) {
    std::string http_metric_name = absl::StrCat(
        config_->metric_name_prefix_, ".", deployment_part, ".http_", http_status_str);
    
    const auto& http_stat_name = dynamic_pool_.add(http_metric_name);
    incCounter(config_->scope_, http_stat_name);
    ENVOY_LOG(debug, "Recorded HTTP metric: {}", http_metric_name);
  }

  // 4. If service/method granularity is enabled, create additional metrics
  if (config_->include_service_method_ && request_names_.has_value()) {
    // gRPC service/method metric
    std::string service_method_grpc_metric = absl::StrCat(
        config_->metric_name_prefix_, ".", deployment_part, ".", 
        request_names_->service_, ".", request_names_->method_, ".", status_name);
    
    const auto& service_method_grpc_stat_name = dynamic_pool_.add(service_method_grpc_metric);
    incCounter(config_->scope_, service_method_grpc_stat_name);
    ENVOY_LOG(debug, "Recorded service/method gRPC metric: {}", service_method_grpc_metric);
    
    // Combined service/method + HTTP + gRPC metric
    std::string service_method_combined_metric = absl::StrCat(
        config_->metric_name_prefix_, ".", deployment_part, ".", 
        request_names_->service_, ".", request_names_->method_, ".http_", http_status_str, ".", status_name);
    
    const auto& service_method_combined_stat_name = dynamic_pool_.add(service_method_combined_metric);
    incCounter(config_->scope_, service_method_combined_stat_name);
    ENVOY_LOG(debug, "Recorded service/method combined metric: {}", service_method_combined_metric);
  }
}

Http::FilterFactoryCb IRMetricFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  ConfigConstSharedPtr config = std::make_shared<const Config>(proto_config, context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<IRMetricFilter>(config));
  };
}

void IRMetricFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

absl::optional<std::string> IRMetricFilter::extractDeploymentFromUpstream() {
  if (!decoder_callbacks_ || !decoder_callbacks_->streamInfo().upstreamInfo()) {
    ENVOY_LOG(debug, "No upstream info available");
    return absl::nullopt;
  }

  const auto& upstream_host = decoder_callbacks_->streamInfo().upstreamInfo()->upstreamHost();
  if (!upstream_host) {
    ENVOY_LOG(debug, "No upstream host available");
    return absl::nullopt;
  }

  const auto& upstream_host_metadata = upstream_host->metadata();
  if (!upstream_host_metadata) {
    ENVOY_LOG(debug, "No upstream host metadata available");
    return absl::nullopt;
  }

  return extractLbMetadataValue(upstream_host_metadata, std::string(DeploymentMetadataKey));
}

absl::optional<std::string> IRMetricFilter::extractLbMetadataValue(
    const Upstream::MetadataConstSharedPtr& upstream_host_metadata,
    const std::string& key_name) {

  // Find LB metadata
  const auto& lb_metadata = upstream_host_metadata->filter_metadata().find(std::string(LBMetadataName));
  if (lb_metadata == upstream_host_metadata->filter_metadata().end()) {
    ENVOY_LOG(debug, "No LB metadata found");
    return absl::nullopt;
  }

  // Find specific key in LB metadata
  const auto& lb_metadata_value = lb_metadata->second.fields().find(key_name);
  if (lb_metadata_value == lb_metadata->second.fields().end()) {
    ENVOY_LOG(debug, "Key '{}' not found in LB metadata", key_name);
    return absl::nullopt;
  }

  // Return the string value
  return lb_metadata_value->second.string_value();
}

// Dynamic stats helpers
void IRMetricFilter::incCounter(Stats::Scope& scope, const Stats::StatName& stat) {
  Stats::Utility::counterFromElements(scope, {stat}).inc();
}

void IRMetricFilter::incGauge(Stats::Scope& scope, const Stats::StatName& stat) {
  Stats::Utility::gaugeFromElements(scope, {stat}, Stats::Gauge::ImportMode::Accumulate).inc();
}

void IRMetricFilter::setGauge(Stats::Scope& scope, const Stats::StatName& stat, uint64_t value) {
  Stats::Utility::gaugeFromElements(scope, {stat}, Stats::Gauge::ImportMode::Accumulate).set(value);
}

/**
 * Static registration for the IR metrics filter. @see RegisterFactory.
 */
REGISTER_FACTORY(IRMetricFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStatusMetrics
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy