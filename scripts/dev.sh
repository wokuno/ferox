#!/usr/bin/env bash
# Ferox - Development Helper Script
# Usage: ./scripts/dev.sh [command]
#
# Provides common development workflows

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

CMD="${1:-help}"
shift 2>/dev/null || true

echo "╔══════════════════════════════════════════╗"
echo "║       Ferox - Development Helper         ║"
echo "╚══════════════════════════════════════════╝"
echo ""

case "$CMD" in
    setup)
        echo "🚀 Setting up development environment..."
        
        # Make all scripts executable
        chmod +x "$SCRIPT_DIR"/*.sh
        
        # Build in debug mode
        "$SCRIPT_DIR/build.sh" debug
        
        # Run quick tests
        "$SCRIPT_DIR/test.sh" quick
        
        echo ""
        echo "✅ Development environment ready!"
        echo ""
        echo "Quick start:"
        echo "  ./scripts/run.sh        # Start server and client"
        echo "  ./scripts/run.sh demo   # Run client in demo mode"
        ;;
    
    rebuild)
        echo "🔄 Rebuilding from scratch..."
        "$SCRIPT_DIR/clean.sh" build
        "$SCRIPT_DIR/build.sh" "${1:-debug}"
        ;;
    
    check)
        echo "🔍 Running pre-commit checks..."
        
        # Build
        "$SCRIPT_DIR/build.sh" debug
        
        # Run tests
        "$SCRIPT_DIR/test.sh" quick
        
        echo ""
        echo "✅ All checks passed!"
        ;;
    
    watch)
        echo "👀 Watching for changes..."
        echo "   (Requires fswatch: brew install fswatch)"
        
        if ! command -v fswatch &> /dev/null; then
            echo "❌ fswatch not installed"
            echo "   Install with: brew install fswatch"
            exit 1
        fi
        
        # Initial build
        "$SCRIPT_DIR/build.sh" debug
        
        # Watch for changes
        fswatch -o "$PROJECT_DIR/src" | while read; do
            echo ""
            echo "🔄 Change detected, rebuilding..."
            "$SCRIPT_DIR/build.sh" debug 2>&1 | tail -5
        done
        ;;
    
    format)
        echo "🎨 Formatting code..."
        
        if command -v clang-format &> /dev/null; then
            find "$PROJECT_DIR/src" -name "*.c" -o -name "*.h" | \
                xargs clang-format -i -style=file
            find "$PROJECT_DIR/tests" -name "*.c" | \
                xargs clang-format -i -style=file
            echo "✅ Code formatted"
        else
            echo "⚠️  clang-format not installed"
            echo "   Install with: brew install clang-format"
        fi
        ;;
    
    lint)
        echo "🔍 Running linter..."
        
        if command -v cppcheck &> /dev/null; then
            cppcheck --enable=all --std=c11 \
                     --suppress=missingIncludeSystem \
                     "$PROJECT_DIR/src" 2>&1 | grep -v "^$"
        else
            echo "⚠️  cppcheck not installed"
            echo "   Install with: brew install cppcheck"
        fi
        ;;
    
    docs)
        echo "📚 Opening documentation..."
        
        if [[ -f "$PROJECT_DIR/docs/README.md" ]]; then
            if command -v glow &> /dev/null; then
                glow "$PROJECT_DIR/docs/README.md"
            elif command -v bat &> /dev/null; then
                bat "$PROJECT_DIR/docs/README.md"
            else
                cat "$PROJECT_DIR/docs/README.md"
            fi
        else
            echo "Documentation not found"
        fi
        ;;
    
    stats)
        echo "📊 Project Statistics"
        echo ""
        
        echo "Source Lines of Code:"
        find "$PROJECT_DIR/src" -name "*.c" -o -name "*.h" | \
            xargs wc -l | tail -1 | awk '{print "  " $1 " lines"}'
        
        echo ""
        echo "Test Lines of Code:"
        find "$PROJECT_DIR/tests" -name "*.c" | \
            xargs wc -l | tail -1 | awk '{print "  " $1 " lines"}'
        
        echo ""
        echo "File Counts:"
        echo "  Source files: $(find "$PROJECT_DIR/src" -name "*.c" | wc -l | tr -d ' ')"
        echo "  Header files: $(find "$PROJECT_DIR/src" -name "*.h" | wc -l | tr -d ' ')"
        echo "  Test files:   $(find "$PROJECT_DIR/tests" -name "*.c" | wc -l | tr -d ' ')"
        echo "  Doc files:    $(find "$PROJECT_DIR/docs" -name "*.md" | wc -l | tr -d ' ')"
        ;;
    
    help|*)
        echo "Usage: ./scripts/dev.sh [command]"
        echo ""
        echo "Commands:"
        echo "  setup    Set up development environment"
        echo "  rebuild  Clean and rebuild from scratch"
        echo "  check    Run pre-commit checks (build + test)"
        echo "  watch    Watch for changes and auto-rebuild"
        echo "  format   Format code with clang-format"
        echo "  lint     Run static analysis with cppcheck"
        echo "  docs     View documentation"
        echo "  stats    Show project statistics"
        echo "  help     Show this help message"
        echo ""
        echo "Other scripts:"
        echo "  ./scripts/build.sh [debug|release]"
        echo "  ./scripts/run.sh [server|client|both|demo]"
        echo "  ./scripts/test.sh [all|unit|stress|phase1...]"
        echo "  ./scripts/clean.sh [build|artifacts|all]"
        ;;
esac
