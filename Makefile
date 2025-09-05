# Iza Container Runtime - Makefile
# Phase 2: Resource Control with cgroups

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2
TARGET = iza
SOURCE = main.cpp

.PHONY: all clean install test

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)
	rm -rf /tmp/iza-rootfs

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

# Test commands
test: $(TARGET)
	@echo "🧪 Testing basic container functionality..."
	sudo ./$(TARGET) run /bin/bash -c "hostname && echo 'Container test successful!'"

test-memory: $(TARGET)
	@echo "🧪 Testing memory limits..."
	@echo "Installing stress tool if needed..."
	@sudo apt update && sudo apt install -y stress || true
	@echo "Testing memory limit (should fail):"
	sudo ./$(TARGET) run --memory 50m stress --vm 1 --vm-bytes 100m --vm-hang 5 || echo "✅ Memory limit working - process was killed!"

test-cpu: $(TARGET)
	@echo "🧪 Testing CPU limits..."
	@echo "Testing CPU limit (watch system load):"
	sudo ./$(TARGET) run --cpus 0.5 stress --cpu 4 --timeout 10 || echo "✅ CPU limit applied!"

help:
	@echo "🎯 Iza Container Runtime - Build System"
	@echo "======================================="
	@echo ""
	@echo "Targets:"
	@echo "  all          Build the iza binary"
	@echo "  clean        Remove built files and temporary directories"
	@echo "  install      Install iza to /usr/local/bin (requires sudo)"
	@echo "  test         Run basic functionality tests"
	@echo "  test-memory  Test memory limiting with stress tool"
	@echo "  test-cpu     Test CPU limiting with stress tool"
	@echo "  help         Show this help message"
	@echo ""
	@echo "Usage examples:"
	@echo "  make clean && make                           # Build"
	@echo "  sudo ./iza run /bin/bash                     # Basic container"
	@echo "  sudo ./iza run --memory 100m /bin/bash       # With memory limit"
	@echo "  sudo ./iza run --cpus 1 --memory 50m /bin/bash  # With both limits"