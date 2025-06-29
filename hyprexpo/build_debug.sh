#!/bin/bash

echo "Building hyprexpo debug version..."

# Create build directory
mkdir -p build_debug
cd build_debug

# Configure with CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-g -O0 -DDEBUG" \
    -DCMAKE_CXX_STANDARD=23

# Build
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo "✅ Debug build successful!"
    echo "Binary: $(pwd)/libhyprexpo.so"
    echo ""
    echo "To test this debug version:"
    echo "1. Copy it to your hyprpm directory:"
    echo "   sudo cp libhyprexpo.so /var/cache/hyprpm/user/hyprexpo/"
    echo "2. Enable the plugin:"
    echo "   hyprpm enable hyprexpo"
    echo "3. Watch for the step-by-step notifications to identify where it crashes"
else
    echo "❌ Debug build failed!"
    exit 1
fi 