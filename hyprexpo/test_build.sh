#!/bin/bash

# Test build script for hyprexpo with new hook system
set -e

echo "Testing hyprexpo build with new hook system..."

# Check if we're in the right directory
if [ ! -f "main.cpp" ]; then
    echo "Error: main.cpp not found. Please run this script from the hyprexpo directory."
    exit 1
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building hyprexpo..."
make -j$(nproc)

echo "Build completed successfully!"
echo "The hyprexpo plugin has been adapted to use the new hook system."

# Check if the binary was created
if [ -f "libhyprexpo.so" ]; then
    echo "✅ hyprexpo plugin built successfully with new hook system"
    echo "Binary: $(pwd)/libhyprexpo.so"
else
    echo "❌ Build failed - libhyprexpo.so not found"
    exit 1
fi 