package main

import (
	"context"
	"fmt"
	"log"
	"net"

	core "github.com/envoyproxy/go-control-plane/envoy/config/core/v3"
	ext_proc "github.com/envoyproxy/go-control-plane/envoy/service/ext_proc/v3"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/types/known/structpb"
)

type server struct {
	ext_proc.UnimplementedExternalProcessorServer
}

// Process implements the bidirectional stream processing
func (s *server) Process(stream ext_proc.ExternalProcessor_ProcessServer) error {
	log.Println("New stream started")
	
	for {
		req, err := stream.Recv()
		if err != nil {
			log.Printf("Error receiving request: %v", err)
			return err
		}

		var response *ext_proc.ProcessingResponse

		switch msg := req.Request.(type) {
		case *ext_proc.ProcessingRequest_RequestHeaders:
			log.Println("Processing request headers")
			response = s.processRequestHeaders(msg.RequestHeaders)
			
		case *ext_proc.ProcessingRequest_ResponseHeaders:
			log.Println("Processing response headers")
			response = s.processResponseHeaders(msg.ResponseHeaders)
			
		case *ext_proc.ProcessingRequest_RequestBody:
			log.Println("Processing request body")
			response = s.processRequestBody(msg.RequestBody)
			
		case *ext_proc.ProcessingRequest_ResponseBody:
			log.Println("Processing response body")
			response = s.processResponseBody(msg.ResponseBody)
			
		default:
			log.Printf("Unknown request type: %T", msg)
			continue
		}

		if response != nil {
			if err := stream.Send(response); err != nil {
				log.Printf("Error sending response: %v", err)
				return err
			}
		}
	}
}

func (s *server) processRequestHeaders(headers *ext_proc.HttpHeaders) *ext_proc.ProcessingResponse {
	log.Printf("Request headers received: %d headers", len(headers.Headers.Headers))
	
	// Create dynamic metadata for the custom namespace
	dynamicMetadata, err := s.createDynamicMetadata()
	if err != nil {
		log.Printf("Error creating dynamic metadata: %v", err)
		return &ext_proc.ProcessingResponse{
			Response: &ext_proc.ProcessingResponse_RequestHeaders{
				RequestHeaders: &ext_proc.HeadersResponse{
					Response: &ext_proc.CommonResponse{
						Status: ext_proc.CommonResponse_CONTINUE,
					},
				},
			},
		}
	}

	// Add custom request header to demonstrate processing
	headerMutation := &ext_proc.HeaderMutation{
		SetHeaders: []*core.HeaderValueOption{
			{
				Header: &core.HeaderValue{
					Key:   "x-ext-proc-processed",
					Value: "true",
				},
			},
		},
	}

	return &ext_proc.ProcessingResponse{
		Response: &ext_proc.ProcessingResponse_RequestHeaders{
			RequestHeaders: &ext_proc.HeadersResponse{
				Response: &ext_proc.CommonResponse{
					Status:         ext_proc.CommonResponse_CONTINUE,
					HeaderMutation: headerMutation,
				},
			},
		},
		// Set the dynamic metadata with custom namespace
		DynamicMetadata: dynamicMetadata,
	}
}

func (s *server) processResponseHeaders(headers *ext_proc.HttpHeaders) *ext_proc.ProcessingResponse {
	log.Printf("Response headers received: %d headers", len(headers.Headers.Headers))
	
	// Add custom response header
	headerMutation := &ext_proc.HeaderMutation{
		SetHeaders: []*core.HeaderValueOption{
			{
				Header: &core.HeaderValue{
					Key:   "x-ext-proc-response",
					Value: "processed",
				},
			},
		},
	}

	return &ext_proc.ProcessingResponse{
		Response: &ext_proc.ProcessingResponse_ResponseHeaders{
			ResponseHeaders: &ext_proc.HeadersResponse{
				Response: &ext_proc.CommonResponse{
					Status:         ext_proc.CommonResponse_CONTINUE,
					HeaderMutation: headerMutation,
				},
			},
		},
	}
}

func (s *server) processRequestBody(body *ext_proc.HttpBody) *ext_proc.ProcessingResponse {
	log.Printf("Request body received: %d bytes", len(body.Body))
	
	return &ext_proc.ProcessingResponse{
		Response: &ext_proc.ProcessingResponse_RequestBody{
			RequestBody: &ext_proc.BodyResponse{
				Response: &ext_proc.CommonResponse{
					Status: ext_proc.CommonResponse_CONTINUE,
				},
			},
		},
	}
}

func (s *server) processResponseBody(body *ext_proc.HttpBody) *ext_proc.ProcessingResponse {
	log.Printf("Response body received: %d bytes", len(body.Body))
	
	return &ext_proc.ProcessingResponse{
		Response: &ext_proc.ProcessingResponse_ResponseBody{
			ResponseBody: &ext_proc.BodyResponse{
				Response: &ext_proc.CommonResponse{
					Status: ext_proc.CommonResponse_CONTINUE,
				},
			},
		},
	}
}

// createDynamicMetadata creates dynamic metadata for the custom namespace
func (s *server) createDynamicMetadata() (*structpb.Struct, error) {
	// Create metadata for the custom filter namespace
	customNamespaceMetadata := map[string]interface{}{
		"deployment": "abc",
		"processed_by": "ext_proc",
		"timestamp": fmt.Sprintf("%d", 1234567890), // You can use actual timestamp
	}
	
	// Convert to structpb.Value
	customMetadataStruct, err := structpb.NewStruct(customNamespaceMetadata)
	if err != nil {
		return nil, fmt.Errorf("failed to create custom metadata struct: %w", err)
	}

	// Create the overall dynamic metadata structure
	dynamicMetadata := map[string]interface{}{
		"envoy.filters.http.mir_stateful_session_process": customMetadataStruct.AsMap(),
	}
	
	// Convert to structpb.Struct
	result, err := structpb.NewStruct(dynamicMetadata)
	if err != nil {
		return nil, fmt.Errorf("failed to create dynamic metadata struct: %w", err)
	}
	
	log.Printf("Created dynamic metadata for namespace 'envoy.filters.http.mir_stateful_session_process' with deployment: abc")
	return result, nil
}

func main() {
	// Listen on port 50051
	lis, err := net.Listen("tcp", ":50051")
	if err != nil {
		log.Fatalf("Failed to listen: %v", err)
	}

	// Create gRPC server
	s := grpc.NewServer()
	ext_proc.RegisterExternalProcessorServer(s, &server{})

	log.Println("Starting ext_proc server on :50051")
	log.Println("Server will update dynamic metadata in namespace 'envoy.filters.http.mir_stateful_session_process'")
	log.Println("Key: 'deployment', Value: 'abc'")
	
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Failed to serve: %v", err)
	}
}