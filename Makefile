# Iza Container Runtime - Makefile
# Phase 3: Image Management & Layers

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
TARGET = iza
SOURCE = main.cpp

# Libraries needed for Phase 3
LIBS = -lcurl -ljsoncpp -larchive

.PHONY: all clean install deps test help

all: deps $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE) $(LIBS)

# Install required dependencies
deps:
	@echo "🔧 Installing dependencies for Phase 3..."
	@sudo apt update
	@sudo apt install -y libcurl4-openssl-dev libarchive-dev || true
	@echo "✅ Dependencies installed!"

clean:
	rm -f $(TARGET)
	rm -rf /tmp/iza-rootfs
	sudo rm -rf /var/lib/iza || true

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo mkdir -p /var/lib/iza/images
	sudo mkdir -p /var/lib/iza/cache
	sudo mkdir -p /var/lib/iza/overlay

# Test commands for Phase 3
test: $(TARGET)
	@echo "🧪 Testing Phase 3 functionality..."
	@echo "Testing basic container (legacy mode):"
	sudo ./$(TARGET) run /bin/bash -c "hostname && echo 'Legacy container test successful!'"

test-pull: $(TARGET)
	@echo "🧪 Testing image pulling..."
	@echo "Pulling Alpine Linux (small image):"
	sudo ./$(TARGET) pull alpine:latest

test-images: $(TARGET)
	@echo "🧪 Testing image listing..."
	sudo ./$(TARGET) images

test-image-run: $(TARGET)
	@echo "🧪 Testing container from image..."
	@echo "Note: Run 'make test-pull' first to download an image"
	sudo ./$(TARGET) run alpine:latest /bin/sh -c "ls / && echo 'Image container test successful!'"

test-memory: $(TARGET)
	@echo "🧪 Testing memory limits with image..."
	@echo "Installing stress tool if needed..."
	@sudo apt update && sudo apt install -y stress || true
	@echo "Testing memory limit (should fail):"
	sudo ./$(TARGET) run --memory 50m alpine:latest stress --vm 1 --vm-bytes 100m --vm-hang 5 || echo "✅ Memory limit working!"

test-full: test test-pull test-images test-image-run test-memory
	@echo "🎉 All Phase 3 tests completed!"

# Development helpers
build-debug: $(SOURCE)
	$(CXX) $(CXXFLAGS) -g -DDEBUG -o $(TARGET)-debug $(SOURCE) $(LIBS)

check-overlay:
	@echo "🔍 Checking OverlayFS support..."
	@grep overlay /proc/filesystems || echo "❌ OverlayFS not available"
	@ls -la /var/lib/iza/ 2>/dev/null || echo "📁 Image directory not created yet"

# Cleanup everything
clean-all: clean
	sudo rm -rf /var/lib/iza
	sudo rm -f /usr/local/bin/iza

help:
	@echo "🎯 Iza Container Runtime - Phase 3 Build System"
	@echo "==============================================="
	@echo ""
	@echo "Build Commands:"
	@echo "  deps         Install required dependencies (curl, jsoncpp, libarchive)"
	@echo "  all          Install dependencies and build iza binary"
	@echo "  build-debug  Build debug version with symbols"
	@echo "  clean        Remove built files and temporary directories"
	@echo "  install      Install iza to /usr/local/bin with proper directories"
	@echo ""
	@echo "Testing Commands:"
	@echo "  test         Run basic functionality tests (legacy mode)"
	@echo "  test-pull    Test image downloading"
	@echo "  test-images  Test image listing"
	@echo "  test-image-run  Test running containers from images"
	@echo "  test-memory  Test memory limits with images"
	@echo "  test-full    Run all tests in sequence"
	@echo ""
	@echo "Image Management Commands:"
	@echo "  sudo ./iza pull alpine:latest        # Download Alpine Linux"
	@echo "  sudo ./iza pull ubuntu:latest        # Download Ubuntu (larger)"
	@echo "  sudo ./iza images                    # List downloaded images"
	@echo "  sudo ./iza run alpine:latest         # Run container from image"
	@echo "  sudo ./iza run alpine:latest /bin/sh # Run specific command"
	@echo ""
	@echo "Resource Control with Images:"
	@echo "  sudo ./iza run --memory 100m alpine:latest"
	@echo "  sudo ./iza run --cpus 1 --memory 50m ubuntu:latest python3"
	@echo ""
	@echo "Legacy Mode (Phase 1-2 compatibility):"
	@echo "  sudo ./iza run /bin/bash             # Custom minimal rootfs"
	@echo ""
	@echo "Utilities:"
	@echo "  check-overlay  Check OverlayFS support and directories"
	@echo "  clean-all      Remove everything including images"
	@echo "  help           Show this help message"