# Iza Container Runtime 

A lightweight container runtime implementation in C++ that demonstrates core containerization concepts including namespaces, cgroups, overlay filesystems, and Docker-style image management.

## Features

- **Container Isolation**: Process, network, filesystem, hostname, and IPC isolation using Linux namespaces
- **Resource Management**: Memory and CPU limits via cgroups v2
- **Image Management**: Pull and manage container images (Alpine Linux, Ubuntu)
- **Overlay Filesystem**: Efficient layered filesystem with OverlayFS support and fallback copying
- **Legacy Support**: Backwards compatible with custom minimal rootfs

## System Requirements

- Linux system with kernel 3.18+ (for namespaces)
- cgroups v2 support (`/sys/fs/cgroup/cgroup.controllers` exists)
- Root privileges (required for namespaces and mounts)
- Development tools: `g++`, `make`
- Dependencies: `libcurl4-openssl-dev`, `libarchive-dev`

## Installation

### 1. Install Dependencies

# Ubuntu/Debian
sudo apt update
sudo apt install -y build-essential libcurl4-openssl-dev libarchive-dev

# Or use the automated installer
make deps


### 2. Build Iza


# Build the binary
make

# Or build with debug symbols
make build-debug


### 3. Install System-wide (Optional)


# Install to /usr/local/bin with proper directories
sudo make install


## Usage

### Image Management

#### Pull Container Images


# Pull Alpine Linux (minimal, ~5MB)
sudo ./iza pull alpine:latest

# Pull Ubuntu (larger, ~80MB)
sudo ./iza pull ubuntu:latest


#### List Downloaded Images


sudo ./iza images


Output:

REPOSITORY          TAG       SIZE
==========================================
alpine              latest    5MB
ubuntu              latest    80MB


### Running Containers

#### Basic Container Execution


# Run interactive shell
sudo ./iza run alpine:latest

# Run specific command
sudo ./iza run alpine:latest /bin/sh -c "ls / && hostname"

# Run with different shell
sudo ./iza run alpine:latest /bin/sh


#### Resource-Limited Containers


# Set memory limit
sudo ./iza run --memory 100m alpine:latest

# Set CPU limit
sudo ./iza run --cpus 1 alpine:latest

# Combined limits
sudo ./iza run --memory 50m --cpus 0.5 ubuntu:latest python3


#### Legacy Mode (Custom Rootfs)


# Run without image (creates minimal custom rootfs)
sudo ./iza run /bin/bash


## Testing

### Automated Tests


# Run all tests
make test-full

# Individual test suites
make test           # Basic functionality
make test-pull      # Image downloading
make test-images    # Image listing
make test-image-run # Container execution
make test-memory    # Resource limits


### Manual Testing

#### 1. Container Isolation Tests


# Test process isolation
sudo ./iza run alpine:latest /bin/sh -c "ps aux"
# Should only show container processes

# Test hostname isolation
sudo ./iza run alpine:latest /bin/sh -c "hostname"
# Should show: iza-container

# Test filesystem isolation
sudo ./iza run alpine:latest /bin/sh -c "ls /"
# Should show Alpine filesystem, not host

#### 2. Network Isolation Test


# Check network interfaces
sudo ./iza run alpine:latest /bin/sh -c "ip addr show"
# Should only show loopback interface


#### 3. Resource Limit Tests


# Install stress tool in Alpine
sudo ./iza run alpine:latest /bin/sh -c "apk add --no-cache stress"

# Test memory limit (should fail/be killed)
sudo ./iza run --memory 50m alpine:latest stress --vm 1 --vm-bytes 100m --vm-hang 5

# Test CPU limit
sudo ./iza run --cpus 0.5 alpine:latest /bin/sh -c "yes > /dev/null &"
# Monitor with htop in another terminal

#### 4. Filesystem Layer Tests


# Create file in container
sudo ./iza run alpine:latest /bin/sh -c "echo 'test' > /tmp/container-file && ls /tmp"

# Check host filesystem (file should not exist)
ls /tmp


## Architecture

### Core Components

1. **Arguments Parser**: Handles command-line interface and validation
2. **ImageManager**: Downloads and manages container images
3. **OverlayFS**: Implements layered filesystem with fallback copying
4. **CgroupManager**: Manages resource limits via cgroups v2
5. **Namespace Manager**: Creates isolated container environments


### Supported Images

- **Alpine Linux**: `alpine:latest` - Minimal Linux distribution (~5MB)
- **Ubuntu**: `ubuntu:latest` - Full Ubuntu base system (~80MB)

## Build System

### Available Make Targets


# Build targets
make deps          # Install dependencies
make all           # Build with dependencies
make build-debug   # Debug build with symbols
make clean         # Clean build files
make install       # System-wide installation

# Testing targets
make test          # Basic tests
make test-pull     # Image download tests
make test-images   # Image listing tests
make test-image-run # Container execution tests
make test-memory   # Resource limit tests
make test-full     # All tests

# Utility targets
make check-overlay # Check OverlayFS support
make clean-all     # Remove everything including images
make help          # Show help information


## Troubleshooting

### Common Issues

#### OverlayFS Not Available


[WARNING] OverlayFS not available, using copy fallback


This is normal on some systems. Iza will automatically use filesystem copying as fallback.

#### Permission Denied


Failed to create container process


Ensure you're running with `sudo`. Container operations require root privileges.

#### cgroups v2 Not Available


Error: cgroups v2 not available


Check if your system supports cgroups v2:

ls /sys/fs/cgroup/cgroup.controllers


#### Image Not Found


Error: Image 'myimage:latest' not found


Pull the image first:

sudo ./iza pull myimage:latest


### Debug Information

#### Check System Capabilities


# Check OverlayFS support
make check-overlay

# Check cgroups v2
ls -la /sys/fs/cgroup/

# Check namespace support
unshare --help


#### Enable Debug Mode


# Build debug version
make build-debug

# Run with verbose output
sudo strace -f ./iza run alpine:latest /bin/sh


## Development

### Code Structure

- `main.cpp`: Complete implementation with all classes
- `Makefile`: Build system with comprehensive targets




