#!/usr/bin/env python3
"""
Simple integration test for FluxGraph gRPC server.
Tests basic connectivity and health check.
"""

import grpc
import sys
import os

# Add generated proto path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'build-server', 'generated'))

try:
    import fluxgraph_pb2
    import fluxgraph_pb2_grpc
except ImportError:
    print("ERROR: Cannot import generated protobuf files.")
    print("Run: python -m grpc_tools.protoc -I../proto --python_out=. --grpc_python_out=. ../proto/fluxgraph.proto")
    sys.exit(1)

def test_health_check(stub):
    """Test the health check RPC"""
    print("Testing health check...")
    request = fluxgraph_pb2.HealthCheckRequest(service="fluxgraph")
    response = stub.Check(request)
    
    if response.status == fluxgraph_pb2.HealthCheckResponse.SERVING:
        print("✓ Health check: SERVING")
        return True
    else:
        print(f"✗ Health check failed: {response.status}")
        return False

def test_load_config(stub, config_path):
    """Test loading a YAML config"""
    print(f"Testing config load from {config_path}...")
    
    with open(config_path, 'r') as f:
        config_content = f.read()
    
    request = fluxgraph_pb2.ConfigRequest(
        config_content=config_content,
        format="yaml",
        config_hash="test_hash_001"
    )
    
    response = stub.LoadConfig(request)
    
    if response.success:
        print(f"✓ Config loaded (changed={response.config_changed})")
        return True
    else:
        print(f"✗ Config load failed: {response.error_message}")
        return False

def test_register_provider(stub):
    """Test provider registration"""
    print("Testing provider registration...")
    
    request = fluxgraph_pb2.ProviderRegistration(
        provider_id="test_provider",
        device_ids=["chamber", "heater"]
    )
    
    response = stub.RegisterProvider(request)
    
    if response.success:
        print(f"✓ Provider registered: session_id={response.session_id}")
        return response.session_id
    else:
        print(f"✗ Provider registration failed: {response.error_message}")
        return None

def main():
    # Connect to server
    server_address =  "localhost:50051"
    print(f"Connecting to FluxGraph server at {server_address}...")
    
    channel = grpc.insecure_channel(server_address)
    stub = fluxgraph_pb2_grpc.FluxGraphStub(channel)
    
    try:
        # Run tests
        results = []
        
        results.append(("Health Check", test_health_check(stub)))
        
        config_path = os.path.join(os.path.dirname(__file__), '..', 'examples', '04_yaml_graph', 'graph.yaml')
        results.append(("Load Config", test_load_config(stub, config_path)))
        
        session_id = test_register_provider(stub)
        results.append(("Register Provider", session_id is not None))
        
        # Summary
        print("\n" + "="*60)
        print("TEST RESULTS")
        print("="*60)
        passed = sum(1 for _, result in results if result)
        total = len(results)
        
        for name, result in results:
            status = "PASS" if result else "FAIL"
            print(f"{name:30s} {status}")
        
        print("="*60)
        print(f"Passed: {passed}/{total}")
        
        return 0 if passed == total else 1
        
    except grpc.RpcError as e:
        print(f"\n✗ gRPC Error: {e.code()} - {e.details()}")
        return 1
    finally:
        channel.close()

if __name__ == "__main__":
    sys.exit(main())
