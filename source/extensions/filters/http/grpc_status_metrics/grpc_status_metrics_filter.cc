#include "source/extensions/filters/http/grpc_status_metrics/grpc_status_metrics_filter.h"

#include "envoy/grpc/status.h"
#include "envoy/registry/registry.h"

#include "source/common/grpc/common.h"
#include "source/common/grpc/status.h"
#include "source/common/grpc/utility.h"
#include "source/common/http/utility.h"
#include "source/common/stats/symbol_table.h"

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
    : config_(config), is_grpc_request_(false) {}

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
    
    // Increment total gRPC requests counter
    config_->stats_.grpc_requests_total_.inc();
    
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
  
  if (status.has_value()) {
    ENVOY_LOG(debug, "Found gRPC status: {}", static_cast<uint64_t>(status.value()));
    recordGrpcStatusMetric(status.value(), http_status);
  } else {
    ENVOY_LOG(debug, "No gRPC status found in headers/trailers");
    // If no gRPC status is found but we know this is a gRPC request,
    // we might want to infer it from HTTP status or treat it as unknown
    if (http_status.has_value()) {
      // Convert HTTP status to gRPC status using utility function
      Grpc::Status::GrpcStatus inferred_status = Grpc::Utility::httpToGrpcStatus(http_status.value());
      recordGrpcStatusMetric(inferred_status, http_status);
    } else {
      // Record as unknown status
      recordGrpcStatusMetric(Grpc::Status::WellKnownGrpcStatus::Unknown);
    }
  }
}

void GrpcStatusMetricsFilter::recordGrpcStatusMetric(Grpc::Status::GrpcStatus status_code,
                                                    absl::optional<uint64_t> http_status) {
  
  // Check if we should emit success metrics
  if (!config_->emit_success_metrics_ && status_code == Grpc::Status::WellKnownGrpcStatus::Ok) {
    return;
  }

  ENVOY_LOG(debug, "Recording gRPC status metric: {} (HTTP: {})", 
            static_cast<uint64_t>(status_code),
            http_status.has_value() ? std::to_string(http_status.value()) : "none");

  // Record the specific status code
  switch (status_code) {
  case Grpc::Status::WellKnownGrpcStatus::Ok:
    config_->stats_.grpc_status_0_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Canceled:
    config_->stats_.grpc_status_1_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Unknown:
    config_->stats_.grpc_status_2_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::InvalidArgument:
    config_->stats_.grpc_status_3_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded:
    config_->stats_.grpc_status_4_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::NotFound:
    config_->stats_.grpc_status_5_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::AlreadyExists:
    config_->stats_.grpc_status_6_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::PermissionDenied:
    config_->stats_.grpc_status_7_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::ResourceExhausted:
    config_->stats_.grpc_status_8_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::FailedPrecondition:
    config_->stats_.grpc_status_9_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Aborted:
    config_->stats_.grpc_status_10_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::OutOfRange:
    config_->stats_.grpc_status_11_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Unimplemented:
    config_->stats_.grpc_status_12_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Internal:
    config_->stats_.grpc_status_13_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Unavailable:
    config_->stats_.grpc_status_14_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::DataLoss:
    config_->stats_.grpc_status_15_.inc();
    break;
  case Grpc::Status::WellKnownGrpcStatus::Unauthenticated:
    config_->stats_.grpc_status_16_.inc();
    break;
  default:
    // For any status codes beyond the well-known ones or invalid codes
    config_->stats_.grpc_status_unknown_.inc();
    break;
  }

  // TODO: If include_service_method_ is true, we could create more granular metrics
  // that include the service and method names as dimensions. This would require
  // dynamic metric creation which increases complexity and cardinality.
}

Http::FilterFactoryCb GrpcStatusMetricsFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_status_metrics::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {

  ConfigConstSharedPtr config = std::make_shared<const Config>(proto_config, context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStatusMetricsFilter>(config));
  };
}

/**
 * Static registration for the gRPC status metrics filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStatusMetricsFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStatusMetrics
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy