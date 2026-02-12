#!/usr/bin/env bash
# Ferox - Build Script
# Usage: ./scripts/build.sh [debug|release] [clean]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Default build type
BUILD_TYPE="${1:-Release}"
CLEAN="${2:-}"

# Normalize build type (compatible with bash 3.x)
BUILD_TYPE_LOWER=$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')
case "$BUILD_TYPE_LOWER" in
    debug|dbg|d)
        BUILD_TYPE="Debug"
        ;;
    release|rel|r)
        BUILD_TYPE="Release"
        ;;
    sanitize|san|s)
        BUILD_TYPE="Debug"
        SANITIZERS=ON
        ;;
    *)
        BUILD_TYPE="Release"
        ;;
esac

echo "╔══════════════════════════════════════════╗"
echo "║       Ferox - Bacterial Simulator        ║"
echo "║              Build Script                ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "Build Type: $BUILD_TYPE"
echo "Build Dir:  $BUILD_DIR"
echo ""

# Clean if requested
if [[ "$CLEAN" == "clean" ]] || [[ "$1" == "clean" ]]; then
    echo "🧹 Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "🔧 Configuring with CMake..."
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"

if [[ "$SANITIZERS" == "ON" ]]; then
    CMAKE_ARGS="$CMAKE_ARGS -DENABLE_SANITIZERS=ON"
    echo "   Sanitizers: ENABLED"
fi

cmake $CMAKE_ARGS "$PROJECT_DIR"

# Build
echo ""
echo "🔨 Building..."
NPROC=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
cmake --build . --parallel "$NPROC"

echo ""
echo "✅ Build complete!"
echo ""
echo "Executables:"
echo "  Server:   $BUILD_DIR/src/server/ferox_server"
echo "  Client:   $BUILD_DIR/src/client/ferox_client"
if [[ -f "$BUILD_DIR/src/gui/ferox_gui" ]]; then
    echo "  GUI:      $BUILD_DIR/src/gui/ferox_gui"
fi
echo ""
echo "Run with:"
echo "  ./scripts/run.sh          # Server + terminal client"
echo "  ./scripts/run.sh gui+     # Server + GUI client"
echo "  ./scripts/run.sh gui      # GUI client only (connect to running server)"
