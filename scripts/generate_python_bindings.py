#!/usr/bin/env python3
"""
Generate Python gRPC bindings from fluxgraph.proto
Run this after installing requirements.txt
"""

import subprocess
import sys
from pathlib import Path

def main():
    # Paths
    repo_root = Path(__file__).parent
    proto_dir = repo_root / "proto"
    proto_file = proto_dir / "fluxgraph.proto"
    output_dir = repo_root / "tests" / "generated"
    
    # Create output directory
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Generate Python bindings
    cmd = [
        sys.executable, "-m", "grpc_tools.protoc",
        f"-I{proto_dir}",
        f"--python_out={output_dir}",
        f"--grpc_python_out={output_dir}",
        str(proto_file)
    ]
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        print(f"Error generating bindings:\n{result.stderr}")
        return 1
    
    print(f"✓ Generated Python bindings in {output_dir}")
    
    # Create __init__.py
    (output_dir / "__init__.py").touch()
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
