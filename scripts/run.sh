#!/usr/bin/env bash
# Ferox - Run Script
# Usage: ./scripts/run.sh [server|client|gui|both] [options]
#
# Examples:
#   ./scripts/run.sh              # Launch both server and client
#   ./scripts/run.sh both         # Launch both server and client
#   ./scripts/run.sh server       # Launch server only
#   ./scripts/run.sh client       # Launch client only (connect to localhost)
#   ./scripts/run.sh gui          # Launch GUI client (connect to localhost)
#   ./scripts/run.sh both -p 9000 # Use custom port

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Executables
SERVER_BIN="$BUILD_DIR/src/server/ferox_server"
CLIENT_BIN="$BUILD_DIR/src/client/ferox_client"
GUI_BIN="$BUILD_DIR/src/gui/ferox_gui"

# Fallback to root directory executables if build dir doesn't exist
if [[ ! -f "$SERVER_BIN" ]]; then
    SERVER_BIN="$PROJECT_DIR/ferox_server"
fi
if [[ ! -f "$CLIENT_BIN" ]]; then
    CLIENT_BIN="$PROJECT_DIR/ferox_client"
fi
if [[ ! -f "$GUI_BIN" ]]; then
    GUI_BIN="$PROJECT_DIR/ferox_gui"
fi

# Default settings
MODE="${1:-both}"

# Handle --help as first argument
if [[ "$MODE" == "--help" ]] || [[ "$MODE" == "-?" ]]; then
    echo "Ferox - Bacterial Colony Simulator"
    echo ""
    echo "Usage: ./scripts/run.sh [mode] [options]"
    echo ""
    echo "Modes:"
    echo "  both    Launch server and terminal client together (default)"
    echo "  server  Launch server only"
    echo "  client  Launch terminal client only"
    echo "  gui     Launch GUI client only (requires SDL2)"
    echo "  gui+    Launch server and GUI client together"
    echo "  demo    Launch terminal client in demo mode (no server)"
    echo ""
    echo "Options:"
    echo "  -p, --port PORT       Server port (default: 8765)"
    echo "  -h, --host HOST       Server host for client (default: localhost)"
    echo "  -w, --width WIDTH     World width (default: 400)"
    echo "  -H, --height HEIGHT   World height (default: 200)"
    echo "  -t, --threads NUM     Thread count (default: 4)"
    echo "  -c, --colonies NUM    Initial colonies (default: 50)"
    echo "  -r, --tick-rate MS    Tick rate in ms (default: 100)"
    echo ""
    echo "Environment variables:"
    echo "  PORT, HOST, WORLD_WIDTH, WORLD_HEIGHT, THREADS, COLONIES, TICK_RATE"
    exit 0
fi

PORT="${PORT:-8765}"
HOST="${HOST:-localhost}"
WORLD_WIDTH="${WORLD_WIDTH:-400}"
WORLD_HEIGHT="${WORLD_HEIGHT:-200}"
THREADS="${THREADS:-4}"
COLONIES="${COLONIES:-50}"
TICK_RATE="${TICK_RATE:-120}"

# Parse additional arguments
shift 2>/dev/null || true
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--port)
            PORT="$2"
            shift 2
            ;;
        -h|--host)
            HOST="$2"
            shift 2
            ;;
        -w|--width)
            WORLD_WIDTH="$2"
            shift 2
            ;;
        -H|--height)
            WORLD_HEIGHT="$2"
            shift 2
            ;;
        -t|--threads)
            THREADS="$2"
            shift 2
            ;;
        -c|--colonies)
            COLONIES="$2"
            shift 2
            ;;
        -r|--tick-rate)
            TICK_RATE="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check if executables exist
check_executable() {
    if [[ ! -f "$1" ]]; then
        echo "❌ Executable not found: $1"
        echo "   Run ./scripts/build.sh first"
        exit 1
    fi
    if [[ ! -x "$1" ]]; then
        chmod +x "$1"
    fi
}

is_port_in_use() {
    local port="$1"
    lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1
}

stop_ferox_server_on_port() {
    local port="$1"
    local pids
    pids="$(lsof -nP -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null || true)"
    [[ -z "$pids" ]] && return 0

    for pid in $pids; do
        local cmd
        cmd="$(ps -p "$pid" -o command= 2>/dev/null || true)"
        if [[ "$cmd" == *ferox_server* ]]; then
            echo "⚠️  Stopping existing ferox server on port $port (PID $pid)..."
            if ! kill "$pid" 2>/dev/null; then
                echo "❌ Failed to send SIGTERM to ferox server PID $pid"
                echo "   Check process ownership/permissions and try again."
                return 1
            fi
            for _ in {1..30}; do
                if ! kill -0 "$pid" 2>/dev/null || ! lsof -nP -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | grep -q "^${pid}$"; then
                    break
                fi
                sleep 0.1
            done
            if kill -0 "$pid" 2>/dev/null; then
                echo "⚠️  PID $pid still alive after SIGTERM, sending SIGKILL..."
                if ! kill -9 "$pid" 2>/dev/null; then
                    echo "❌ Could not force-stop existing ferox server PID $pid"
                    return 1
                fi
                for _ in {1..20}; do
                    if ! kill -0 "$pid" 2>/dev/null || ! lsof -nP -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | grep -q "^${pid}$"; then
                        break
                    fi
                    sleep 0.1
                done
                if kill -0 "$pid" 2>/dev/null && lsof -nP -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | grep -q "^${pid}$"; then
                    echo "❌ Could not stop existing ferox server PID $pid"
                    return 1
                fi
            fi
        else
            echo "❌ Port $port is in use by a non-ferox process."
            echo "   Refusing to kill it automatically."
            lsof -nP -iTCP:"$port" -sTCP:LISTEN || true
            return 1
        fi
    done

    return 0
}

# Cleanup function
cleanup() {
    echo ""
    echo "🛑 Shutting down..."
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    if [[ -n "$CLIENT_PID" ]]; then
        kill "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi
    echo "👋 Goodbye!"
    exit 0
}

trap cleanup SIGINT SIGTERM

echo "╔══════════════════════════════════════════╗"
echo "║       Ferox - Bacterial Simulator        ║"
echo "╚══════════════════════════════════════════╝"
echo ""

case "$MODE" in
    server)
        check_executable "$SERVER_BIN"
        if is_port_in_use "$PORT"; then
            stop_ferox_server_on_port "$PORT" || exit 1
        fi
        echo "🦠 Starting server on port $PORT..."
        echo "   World: ${WORLD_WIDTH}x${WORLD_HEIGHT}"
        echo "   Threads: $THREADS"
        echo "   Colonies: $COLONIES"
        echo "   Tick rate: ${TICK_RATE}ms"
        echo ""
        "$SERVER_BIN" -p "$PORT" -w "$WORLD_WIDTH" -H "$WORLD_HEIGHT" \
                      -t "$THREADS" -c "$COLONIES" -r "$TICK_RATE"
        ;;
    
    client)
        check_executable "$CLIENT_BIN"
        echo "🖥️  Connecting to $HOST:$PORT..."
        echo ""
        "$CLIENT_BIN" -h "$HOST" -p "$PORT"
        ;;
    
    demo)
        check_executable "$CLIENT_BIN"
        echo "🎮 Starting demo mode (no server)..."
        echo ""
        "$CLIENT_BIN" --demo
        ;;
    
    gui)
        check_executable "$GUI_BIN"
        echo "🖼️  Starting GUI client..."
        echo "   Connecting to $HOST:$PORT"
        echo ""
        "$GUI_BIN" -h "$HOST" -p "$PORT"
        ;;
    
    gui+)
        check_executable "$SERVER_BIN"
        check_executable "$GUI_BIN"
        if is_port_in_use "$PORT"; then
            stop_ferox_server_on_port "$PORT" || exit 1
        fi

        echo "🦠 Starting server on port $PORT..."
        echo "   World: ${WORLD_WIDTH}x${WORLD_HEIGHT}"
        echo "   Threads: $THREADS"
        echo "   Colonies: $COLONIES"
        echo ""
        
        # Start server in background, ignoring SIGINT so Ctrl+C on GUI
        # doesn't kill the server (cleanup handles server shutdown)
        ( trap '' INT
          exec "$SERVER_BIN" -p "$PORT" -w "$WORLD_WIDTH" -H "$WORLD_HEIGHT" \
                        -t "$THREADS" -c "$COLONIES" -r "$TICK_RATE"
        ) &
        SERVER_PID=$!
        
        # Wait for server to start
        echo "⏳ Waiting for server to start..."
        sleep 1
        
        # Check if server is still running
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "❌ Server failed to start"
            exit 1
        fi
        
        echo "🖼️  Starting GUI client..."
        echo ""
        
        # Start GUI client in foreground (blocks until user quits)
        "$GUI_BIN" -h "$HOST" -p "$PORT" || true
        
        # Cleanup server after GUI exits
        cleanup
        ;;
    
    both)
        check_executable "$SERVER_BIN"
        check_executable "$CLIENT_BIN"
        if is_port_in_use "$PORT"; then
            stop_ferox_server_on_port "$PORT" || exit 1
        fi

        echo "🦠 Starting server on port $PORT..."
        echo "   World: ${WORLD_WIDTH}x${WORLD_HEIGHT}"
        echo "   Threads: $THREADS"
        echo "   Colonies: $COLONIES"
        echo ""
        
        # Start server in background, ignoring SIGINT so Ctrl+C on client
        # doesn't kill the server (cleanup handles server shutdown)
        ( trap '' INT
          exec "$SERVER_BIN" -p "$PORT" -w "$WORLD_WIDTH" -H "$WORLD_HEIGHT" \
                        -t "$THREADS" -c "$COLONIES" -r "$TICK_RATE"
        ) &
        SERVER_PID=$!
        
        # Wait for server to start
        echo "⏳ Waiting for server to start..."
        sleep 1
        
        # Check if server is still running
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo "❌ Server failed to start"
            exit 1
        fi
        
        echo "🖥️  Starting client..."
        echo ""
        
        # Start client in foreground (blocks until user quits)
        "$CLIENT_BIN" -h "$HOST" -p "$PORT" || true
        
        # Cleanup server after client exits
        cleanup
        ;;
    
    *)
        echo "Unknown mode: $MODE"
        echo "Use: server, client, gui, gui+, demo, or both"
        echo "Run with --help for more info"
        exit 1
        ;;
esac
