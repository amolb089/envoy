# Envoy External Processing (ext_proc) Dynamic Metadata Update

This repository demonstrates how to use Envoy's External Processing filter to update dynamic metadata for custom HTTP filter namespaces. Specifically, it shows how to update metadata in the namespace `envoy.filters.http.mir_stateful_session_process` with key `deployment` and value `abc`.

## Overview

The External Processing (ext_proc) filter allows external services to process HTTP requests and responses, including the ability to update dynamic metadata that can be consumed by other filters in the processing chain.

## Key Features

- **Dynamic Metadata Updates**: Update metadata for custom filter namespaces
- **Bidirectional gRPC Stream**: Real-time processing of HTTP requests/responses
- **Header Manipulation**: Add, modify, or remove HTTP headers
- **Custom Namespace Support**: Target specific filter namespaces like `envoy.filters.http.mir_stateful_session_process`

## Files Structure

```
├── envoy-ext-proc-config.yaml    # Envoy configuration with ext_proc filter
├── ext_proc_server.go            # Go implementation of ext_proc server
├── python_ext_proc_server.py     # Python implementation of ext_proc server
├── go.mod                        # Go module dependencies
├── requirements.txt              # Python dependencies
└── README.md                     # This file
```

## Configuration

### Envoy Configuration

The `envoy-ext-proc-config.yaml` file contains the Envoy configuration that:

1. **Configures the ext_proc filter** with proper metadata options
2. **Sets up receiving namespaces** to allow updates to the custom namespace
3. **Defines the gRPC service** connection to the external processor

Key configuration sections:

```yaml
metadata_options:
  forwarding_namespaces:
    untyped:
    - envoy.filters.http.mir_stateful_session_process
  receiving_namespaces:
    untyped:
    - envoy.filters.http.mir_stateful_session_process
```

### Processing Mode

The configuration uses the following processing mode:
- **Request headers**: SEND (allows processing and metadata updates)
- **Response headers**: SEND (allows processing)
- **Request/Response bodies**: NONE (not processed)
- **Trailers**: SKIP (not processed)

## Implementation Examples

### Go Implementation

The Go server (`ext_proc_server.go`) demonstrates:

```go
// Create dynamic metadata for the custom namespace
dynamicMetadata := map[string]interface{}{
    "envoy.filters.http.mir_stateful_session_process": map[string]interface{}{
        "deployment": "abc",
        "processed_by": "ext_proc",
        "timestamp": "1234567890",
    },
}
```

Key features:
- Uses `github.com/envoyproxy/go-control-plane` for protobuf definitions
- Implements bidirectional gRPC streaming
- Updates metadata in the `ProcessingResponse.DynamicMetadata` field

### Python Implementation

The Python server (`python_ext_proc_server.py`) demonstrates:

```python
# Create the metadata for the custom filter namespace
custom_metadata = Struct()
custom_metadata.fields["deployment"].string_value = "abc"
custom_metadata.fields["processed_by"].string_value = "ext_proc_python"

# Create the overall dynamic metadata structure
dynamic_metadata = Struct()
dynamic_metadata.fields["envoy.filters.http.mir_stateful_session_process"].struct_value.CopyFrom(custom_metadata)
```

## Running the Setup

### Prerequisites

- Envoy proxy (v1.22.0 or later recommended)
- Go 1.21+ (for Go implementation)
- Python 3.8+ (for Python implementation)

### Steps

1. **Start the external processor server**

   For Go:
   ```bash
   cd /path/to/project
   go mod download
   go run ext_proc_server.go
   ```

   For Python:
   ```bash
   pip install -r requirements.txt
   python python_ext_proc_server.py
   ```

2. **Start Envoy with the configuration**
   ```bash
   envoy -c envoy-ext-proc-config.yaml
   ```

3. **Test the setup**
   ```bash
   curl -v http://localhost:10000/anything
   ```

### Expected Behavior

When a request is processed:

1. **Envoy sends request headers** to the ext_proc server
2. **ext_proc server processes the request** and creates dynamic metadata:
   ```json
   {
     "envoy.filters.http.mir_stateful_session_process": {
       "deployment": "abc",
       "processed_by": "ext_proc",
       "timestamp": "1234567890"
     }
   }
   ```
3. **Envoy receives the response** with the dynamic metadata
4. **Other filters** in the chain can access this metadata

### Verification

You can verify the metadata is being set by:

1. **Checking the ext_proc server logs** for metadata creation messages
2. **Adding debug headers** that reflect the metadata values
3. **Using Envoy's admin interface** to inspect filter state
4. **Configuring access logs** to include dynamic metadata

## Metadata Structure

The dynamic metadata structure follows this pattern:

```yaml
dynamic_metadata:
  envoy.filters.http.mir_stateful_session_process:
    deployment: "abc"
    processed_by: "ext_proc"
    timestamp: "1234567890"
```

## Important Notes

1. **Namespace Configuration**: The `metadata_options.receiving_namespaces` must include your custom namespace
2. **Processing Mode**: Request headers must be processed (SEND mode) to update metadata
3. **Timing**: Metadata updates occur during request header processing
4. **Persistence**: Metadata persists for the duration of the request/response cycle
5. **Access**: Other filters can access this metadata using the stream info interface

## Troubleshooting

### Common Issues

1. **Metadata not received**: Ensure `receiving_namespaces` includes your namespace
2. **Connection refused**: Verify the ext_proc server is running on the correct port
3. **Processing timeout**: Check `message_timeout` configuration in Envoy
4. **Protobuf errors**: Ensure correct protobuf definitions and versions

### Debug Tips

1. **Enable debug logging** in both Envoy and the ext_proc server
2. **Use failure_mode_allow: true** during development
3. **Check the Envoy admin interface** at `localhost:9901/stats` for ext_proc statistics
4. **Monitor gRPC stream health** and message counts

## References

- [Envoy External Processing Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/ext_proc_filter)
- [External Processing Proto Definition](https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/ext_proc/v3/external_processor.proto)
- [Dynamic Metadata in Envoy](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/advanced/data_sharing_between_filters)
- [Envoy Go Control Plane](https://github.com/envoyproxy/go-control-plane)
