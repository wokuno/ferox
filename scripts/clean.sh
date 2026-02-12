#!/usr/bin/env bash
# Ferox - Clean Script
# Usage: ./scripts/clean.sh [all|build|artifacts]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

MODE="${1:-build}"

echo "╔══════════════════════════════════════════╗"
echo "║       Ferox - Bacterial Simulator        ║"
echo "║              Clean Script                ║"
echo "╚══════════════════════════════════════════╝"
echo ""

case "$MODE" in
    build)
        echo "🧹 Cleaning build directory..."
        rm -rf "$PROJECT_DIR/build"
        rm -rf "$PROJECT_DIR/build-coverage"
        rm -rf "$PROJECT_DIR/cmake-build-*"
        echo "✅ Build directories cleaned"
        ;;
    
    artifacts)
        echo "🧹 Cleaning build artifacts..."
        rm -rf "$PROJECT_DIR/build"
        rm -rf "$PROJECT_DIR/build-coverage"
        rm -rf "$PROJECT_DIR/cmake-build-*"
        rm -f "$PROJECT_DIR/ferox_server"
        rm -f "$PROJECT_DIR/ferox_client"
        rm -f "$PROJECT_DIR"/*.o
        rm -f "$PROJECT_DIR"/*.a
        find "$PROJECT_DIR" -name "*.o" -delete
        find "$PROJECT_DIR" -name "*.a" -delete
        find "$PROJECT_DIR" -name "*.gcno" -delete
        find "$PROJECT_DIR" -name "*.gcda" -delete
        echo "✅ All artifacts cleaned"
        ;;
    
    all)
        echo "🧹 Deep cleaning everything..."
        rm -rf "$PROJECT_DIR/build"
        rm -rf "$PROJECT_DIR/build-coverage"
        rm -rf "$PROJECT_DIR/cmake-build-*"
        rm -f "$PROJECT_DIR/ferox_server"
        rm -f "$PROJECT_DIR/ferox_client"
        rm -f "$PROJECT_DIR/CMakeCache.txt"
        rm -rf "$PROJECT_DIR/CMakeFiles"
        find "$PROJECT_DIR" -name "*.o" -delete
        find "$PROJECT_DIR" -name "*.a" -delete
        find "$PROJECT_DIR" -name "*.gcno" -delete
        find "$PROJECT_DIR" -name "*.gcda" -delete
        find "$PROJECT_DIR" -name ".DS_Store" -delete
        echo "✅ Everything cleaned"
        ;;
    
    *)
        echo "Usage: ./scripts/clean.sh [mode]"
        echo ""
        echo "Modes:"
        echo "  build      Clean build directories only (default)"
        echo "  artifacts  Clean all build artifacts"
        echo "  all        Deep clean everything"
        exit 1
        ;;
esac
