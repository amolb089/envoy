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

GrpcStatusMetricsFilter::GrpcStatusMetricsFilter(ConfigConstSharedPtr config)
    : config_(config), is_grpc_request_(false), dynamic_pool_(config->scope_.symbolTable()) {}

Http::FilterHeadersStatus GrpcStatusMetricsFilter::decodeHeaders(Http::RequestHeaderMap& headers,
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

Http::FilterHeadersStatus GrpcStatusMetricsFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                 bool end_stream) {
  if (!is_grpc_request_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Check if this is a gRPC response
  bool is_grpc_response = Grpc::Common::isGrpcResponseHeaders(headers, end_stream);
  
  if (is_grpc_response) {
    ENVOY_LOG(debug, "Detected gRPC response");
    
    // Extract deployment name from upstream metadata for dimension
    deployment_name_ = extractDeploymentFromUpstream();
    if (deployment_name_.has_value()) {
      ENVOY_LOG(debug, "Found deployment: {}", deployment_name_.value());
    }
    
    // Extract HTTP status code if configured
    absl::optional<uint64_t> http_status;
    if (config_->include_http_status_) {
      http_status = Http::Utility::getResponseStatus(headers);
    }
    
    // Try to extract gRPC status from headers
    extractAndRecordStatus(headers, http_status);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterTrailersStatus GrpcStatusMetricsFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
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

void GrpcStatusMetricsFilter::extractAndRecordStatus(
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

void GrpcStatusMetricsFilter::recordGrpcStatusMetric(Grpc::Status::GrpcStatus status_code,
                                                    absl::optional<uint64_t> http_status,
                                                    const std::string& deployment) {
  
  // Check if we should emit success metrics
  if (!config_->emit_success_metrics_ && status_code == Grpc::Status::WellKnownGrpcStatus::Ok) {
    return;
  }

  ENVOY_LOG(debug, "Recording gRPC status metric: {} (HTTP: {}, Deployment: {})", 
            static_cast<uint64_t>(status_code),
            http_status.has_value() ? std::to_string(http_status.value()) : "none",
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

  // Create dynamic metric name with deployment dimension
  std::string metric_name;
  if (!deployment.empty()) {
    metric_name = absl::StrCat(config_->metric_name_prefix_, ".", deployment, ".", status_name);
  } else {
    metric_name = absl::StrCat(config_->metric_name_prefix_, ".unknown_deployment.", status_name);
  }

  // Record the metric using dynamic stats
  const auto& stat_name = dynamic_pool_.add(metric_name);
  incCounter(config_->scope_, stat_name);
  
  ENVOY_LOG(debug, "Recorded metric: {}", metric_name);

  // If service/method granularity is enabled, create additional metrics
  if (config_->include_service_method_ && request_names_.has_value()) {
    std::string service_method_metric;
    if (!deployment.empty()) {
      service_method_metric = absl::StrCat(
          config_->metric_name_prefix_, ".", deployment, ".", 
          request_names_->service_, ".", request_names_->method_, ".", status_name);
    } else {
      service_method_metric = absl::StrCat(
          config_->metric_name_prefix_, ".unknown_deployment.", 
          request_names_->service_, ".", request_names_->method_, ".", status_name);
    }
    
    const auto& service_method_stat_name = dynamic_pool_.add(service_method_metric);
    incCounter(config_->scope_, service_method_stat_name);
    
    ENVOY_LOG(debug, "Recorded service/method metric: {}", service_method_metric);
  }
}

Http::FilterFactoryCb GrpcStatusMetricsFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  ConfigConstSharedPtr config = std::make_shared<const Config>(proto_config, context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStatusMetricsFilter>(config));
  };
}

void GrpcStatusMetricsFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

absl::optional<std::string> GrpcStatusMetricsFilter::extractDeploymentFromUpstream() {
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

absl::optional<std::string> GrpcStatusMetricsFilter::extractLbMetadataValue(
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
void GrpcStatusMetricsFilter::incCounter(Stats::Scope& scope, const Stats::StatName& stat) {
  Stats::Utility::counterFromElements(scope, {stat}).inc();
}

void GrpcStatusMetricsFilter::incGauge(Stats::Scope& scope, const Stats::StatName& stat) {
  Stats::Utility::gaugeFromElements(scope, {stat}, Stats::Gauge::ImportMode::Accumulate).inc();
}

void GrpcStatusMetricsFilter::setGauge(Stats::Scope& scope, const Stats::StatName& stat, uint64_t value) {
  Stats::Utility::gaugeFromElements(scope, {stat}, Stats::Gauge::ImportMode::Accumulate).set(value);
}

/**
 * Static registration for the gRPC status metrics filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStatusMetricsFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStatusMetrics
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy