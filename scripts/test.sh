#!/usr/bin/env bash
# Ferox - Test Script
# Usage: ./scripts/test.sh [all|unit|stress|perf|phase1|phase2|...] [verbose]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
TESTS_DIR="$PROJECT_DIR/tests"

# Test category
CATEGORY="${1:-all}"
VERBOSE="${2:-}"

echo "╔══════════════════════════════════════════╗"
echo "║       Ferox - Bacterial Simulator        ║"
echo "║              Test Runner                 ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Ensure build exists
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "🔨 Build not found, building first..."
    "$SCRIPT_DIR/build.sh" debug
    echo ""
fi

cd "$BUILD_DIR"

# Run tests based on category
run_ctest() {
    local filter="$1"
    local name="$2"
    
    echo "🧪 Running $name..."
    echo ""
    
    if [[ "$VERBOSE" == "verbose" ]] || [[ "$VERBOSE" == "-v" ]]; then
        ctest --output-on-failure -R "$filter" -V
    else
        ctest --output-on-failure -R "$filter"
    fi
}

run_single_test() {
    local test_name="$1"
    local test_bin="$BUILD_DIR/tests/$test_name"
    
    if [[ -f "$test_bin" ]]; then
        echo "🧪 Running $test_name..."
        "$test_bin"
    else
        echo "❌ Test not found: $test_bin"
        return 1
    fi
}

case "$CATEGORY" in
    all)
        echo "🧪 Running all tests..."
        echo ""
        if [[ "$VERBOSE" == "verbose" ]] || [[ "$VERBOSE" == "-v" ]]; then
            ctest --output-on-failure -V
        else
            ctest --output-on-failure
        fi
        ;;
    
    unit)
        run_ctest "Phase|test_" "unit tests"
        ;;
    
    stress)
        run_ctest "stress" "stress tests"
        ;;

    perf|performance)
        echo "⚡ Running performance evaluation tests..."
        echo "   Tip: set FEROX_PERF_SCALE=2 (or higher) for heavier timing loops"
        echo ""
        echo "🧪 Running SIMD + performance eval tests (verbose timing output)..."
        echo ""
        ctest --output-on-failure -R "SimdEvalTests|PerformanceEvalTests" -V
        ;;
    
    phase1)
        run_ctest "Phase1" "Phase 1 tests (types, names, colors, utils)"
        ;;
    
    phase2)
        run_ctest "Phase2" "Phase 2 tests (world, genetics, simulation)"
        ;;
    
    phase3)
        run_ctest "Phase3" "Phase 3 tests (threading, parallel)"
        ;;
    
    phase4)
        run_ctest "Phase4" "Phase 4 tests (protocol, network)"
        ;;
    
    phase5)
        run_ctest "Phase5" "Phase 5 tests (server)"
        ;;
    
    phase6)
        run_ctest "Phase6" "Phase 6 tests (client)"
        ;;
    
    genetics)
        run_ctest "genetics" "genetics tests"
        ;;
    
    world)
        run_ctest "world" "world tests"
        ;;
    
    protocol)
        run_ctest "protocol" "protocol tests"
        ;;
    
    names)
        run_ctest "names" "name generation tests"
        ;;
    
    colors)
        run_ctest "colors" "color tests"
        ;;
    
    threadpool)
        run_ctest "threadpool" "thread pool tests"
        ;;
    
    list)
        echo "Available tests:"
        ctest -N
        ;;
    
    coverage)
        echo "📊 Running tests with coverage..."
        
        # Rebuild with coverage flags
        cd "$PROJECT_DIR"
        mkdir -p build-coverage
        cd build-coverage
        cmake -DCMAKE_BUILD_TYPE=Debug \
              -DCMAKE_C_FLAGS="--coverage -fprofile-arcs -ftest-coverage" \
              "$PROJECT_DIR"
        cmake --build . --parallel
        
        # Run tests
        ctest --output-on-failure
        
        # Generate coverage report
        if command -v lcov &> /dev/null; then
            lcov --capture --directory . --output-file coverage.info
            lcov --remove coverage.info '/usr/*' --output-file coverage.info
            lcov --list coverage.info
            
            if command -v genhtml &> /dev/null; then
                genhtml coverage.info --output-directory coverage-report
                echo ""
                echo "📊 Coverage report: $PROJECT_DIR/build-coverage/coverage-report/index.html"
            fi
        else
            echo "⚠️  lcov not installed, skipping coverage report"
        fi
        ;;
    
    quick)
        echo "⚡ Running quick tests (no stress tests)..."
        ctest --output-on-failure -E "stress"
        ;;
    
    *)
        # Try to run as a specific test name
        if ctest -N -R "$CATEGORY" | grep -q "$CATEGORY"; then
            run_ctest "$CATEGORY" "$CATEGORY tests"
        else
            echo "Unknown test category: $CATEGORY"
            echo ""
            echo "Usage: ./scripts/test.sh [category] [verbose]"
            echo ""
            echo "Categories:"
            echo "  all       Run all tests (default)"
            echo "  unit      Run all unit tests"
            echo "  stress    Run stress tests"
            echo "  perf      Run SIMD/performance evaluation tests"
            echo "  quick     Run tests excluding stress tests"
            echo "  phase1    Phase 1 tests (types, names, colors)"
            echo "  phase2    Phase 2 tests (world, genetics)"
            echo "  phase3    Phase 3 tests (threading)"
            echo "  phase4    Phase 4 tests (protocol)"
            echo "  phase5    Phase 5 tests (server)"
            echo "  phase6    Phase 6 tests (client)"
            echo "  genetics  Genetics-specific tests"
            echo "  world     World-specific tests"
            echo "  names     Name generation tests"
            echo "  colors    Color tests"
            echo "  list      List all available tests"
            echo "  coverage  Run with code coverage"
            echo ""
            echo "Options:"
            echo "  verbose   Show detailed test output"
            exit 1
        fi
        ;;
esac

echo ""
echo "✅ Tests complete!"
