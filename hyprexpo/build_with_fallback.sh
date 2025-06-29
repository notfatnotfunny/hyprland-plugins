#!/bin/bash

# Build script with fallback for hyprexpo plugin
set -e

echo "Building hyprexpo plugin with fallback support..."

# Check if we're in the right directory
if [ ! -f "main.cpp" ]; then
    echo "Error: main.cpp not found. Please run this script from the hyprexpo directory."
    exit 1
fi

# Create build directory
mkdir -p build
cd build

# Try building with new hook system first
echo "Attempting to build with new hook system..."
if cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc); then
    echo "✅ Successfully built with new hook system"
    echo "Binary: $(pwd)/libhyprexpo.so"
    echo "This version uses the new CHookSystem for better ARM64 support."
else
    echo "❌ Failed to build with new hook system, trying fallback..."
    
    # Clean and try with fallback
    make clean
    rm -f CMakeCache.txt
    
    # Replace main.cpp with fallback version
    cp ../main_fallback.cpp ../main.cpp.backup
    cp ../main_fallback.cpp ../main.cpp
    
    if cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc); then
        echo "✅ Successfully built with fallback hook system"
        echo "Binary: $(pwd)/libhyprexpo.so"
        echo "This version uses the old CFunctionHook system as fallback."
        
        # Restore original main.cpp
        mv ../main.cpp.backup ../main.cpp
    else
        echo "❌ Both build attempts failed"
        # Restore original main.cpp
        mv ../main.cpp.backup ../main.cpp
        exit 1
    fi
fi

echo "Build completed successfully!" 