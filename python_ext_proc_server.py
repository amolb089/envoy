#!/usr/bin/env python3

import asyncio
import logging
from typing import AsyncIterable

import grpc
from grpc import aio
from google.protobuf.struct_pb2 import Struct, Value

# Import Envoy protobuf definitions
from envoy.service.ext_proc.v3 import external_processor_pb2 as ext_proc
from envoy.service.ext_proc.v3 import external_processor_pb2_grpc as ext_proc_grpc
from envoy.config.core.v3 import base_pb2 as core

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)


class ExternalProcessorService(ext_proc_grpc.ExternalProcessorServicer):
    """
    External processor service that updates dynamic metadata for custom namespace
    'envoy.filters.http.mir_stateful_session_process' with key 'deployment' and value 'abc'
    """

    async def Process(
        self,
        request_iterator: AsyncIterable[ext_proc.ProcessingRequest],
        context: grpc.aio.ServicerContext,
    ) -> AsyncIterable[ext_proc.ProcessingResponse]:
        """Process the bidirectional stream of requests from Envoy"""
        logger.info("New stream started")
        
        async for request in request_iterator:
            response = None
            
            if request.HasField("request_headers"):
                logger.info("Processing request headers")
                response = self._process_request_headers(request.request_headers)
                
            elif request.HasField("response_headers"):
                logger.info("Processing response headers")
                response = self._process_response_headers(request.response_headers)
                
            elif request.HasField("request_body"):
                logger.info("Processing request body")
                response = self._process_request_body(request.request_body)
                
            elif request.HasField("response_body"):
                logger.info("Processing response body")
                response = self._process_response_body(request.response_body)
                
            else:
                logger.warning(f"Unknown request type: {type(request)}")
                continue
                
            if response:
                yield response

    def _process_request_headers(self, headers: ext_proc.HttpHeaders) -> ext_proc.ProcessingResponse:
        """Process request headers and add dynamic metadata"""
        logger.info(f"Request headers received: {len(headers.headers.headers)} headers")
        
        # Create dynamic metadata for the custom namespace
        dynamic_metadata = self._create_dynamic_metadata()
        
        # Create header mutation to add a custom header
        header_mutation = ext_proc.HeaderMutation()
        header_option = core.HeaderValueOption()
        header_option.header.key = "x-ext-proc-processed"
        header_option.header.value = "true"
        header_mutation.set_headers.append(header_option)
        
        # Create the response
        response = ext_proc.ProcessingResponse()
        response.request_headers.response.status = ext_proc.CommonResponse.CONTINUE
        response.request_headers.response.header_mutation.CopyFrom(header_mutation)
        
        # Set the dynamic metadata
        response.dynamic_metadata.CopyFrom(dynamic_metadata)
        
        return response

    def _process_response_headers(self, headers: ext_proc.HttpHeaders) -> ext_proc.ProcessingResponse:
        """Process response headers"""
        logger.info(f"Response headers received: {len(headers.headers.headers)} headers")
        
        # Add custom response header
        header_mutation = ext_proc.HeaderMutation()
        header_option = core.HeaderValueOption()
        header_option.header.key = "x-ext-proc-response"
        header_option.header.value = "processed"
        header_mutation.set_headers.append(header_option)
        
        response = ext_proc.ProcessingResponse()
        response.response_headers.response.status = ext_proc.CommonResponse.CONTINUE
        response.response_headers.response.header_mutation.CopyFrom(header_mutation)
        
        return response

    def _process_request_body(self, body: ext_proc.HttpBody) -> ext_proc.ProcessingResponse:
        """Process request body"""
        logger.info(f"Request body received: {len(body.body)} bytes")
        
        response = ext_proc.ProcessingResponse()
        response.request_body.response.status = ext_proc.CommonResponse.CONTINUE
        
        return response

    def _process_response_body(self, body: ext_proc.HttpBody) -> ext_proc.ProcessingResponse:
        """Process response body"""
        logger.info(f"Response body received: {len(body.body)} bytes")
        
        response = ext_proc.ProcessingResponse()
        response.response_body.response.status = ext_proc.CommonResponse.CONTINUE
        
        return response

    def _create_dynamic_metadata(self) -> Struct:
        """
        Create dynamic metadata for the custom namespace
        'envoy.filters.http.mir_stateful_session_process'
        """
        # Create the metadata for the custom filter namespace
        custom_metadata = Struct()
        custom_metadata.fields["deployment"].string_value = "abc"
        custom_metadata.fields["processed_by"].string_value = "ext_proc_python"
        custom_metadata.fields["timestamp"].string_value = "1234567890"
        
        # Create the overall dynamic metadata structure
        dynamic_metadata = Struct()
        dynamic_metadata.fields["envoy.filters.http.mir_stateful_session_process"].struct_value.CopyFrom(custom_metadata)
        
        logger.info("Created dynamic metadata for namespace 'envoy.filters.http.mir_stateful_session_process' with deployment: abc")
        return dynamic_metadata


async def serve():
    """Start the gRPC server"""
    server = aio.server()
    
    # Add the external processor service
    ext_proc_grpc.add_ExternalProcessorServicer_to_server(
        ExternalProcessorService(), server
    )
    
    # Listen on port 50051
    listen_addr = "[::]:50051"
    server.add_insecure_port(listen_addr)
    
    logger.info("Starting ext_proc server on :50051")
    logger.info("Server will update dynamic metadata in namespace 'envoy.filters.http.mir_stateful_session_process'")
    logger.info("Key: 'deployment', Value: 'abc'")
    
    await server.start()
    
    try:
        await server.wait_for_termination()
    except KeyboardInterrupt:
        logger.info("Shutting down server...")
        await server.stop(grace=5)


if __name__ == "__main__":
    asyncio.run(serve())