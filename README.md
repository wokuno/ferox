# Ferox - Bacterial Colony Simulator

A multi-threaded bacterial colony simulation with client-server architecture.

## Features

- **Real-time bacterial colony simulation** with genetic evolution
- **Multi-threaded simulation engine** with lock-free atomic operations
- **Client-server architecture** for remote viewing
- **Genetics system** - mutation, speciation, family-based recombination
- **Combat mechanics** - aggression vs resilience traits determine territorial disputes
- **Size-based decay** - larger colonies decay faster (resource transport limitation)
- **Social behavior** - chemotaxis-like attraction/repulsion between colonies
- **Environmental systems** - nutrients, toxins, chemical signals
- **Terminal client** - 24-bit ANSI color rendering
- **GUI client (SDL2)** - grid-based rendering with zoom/pan
- **Demo mode** - standalone visualization without server

## Current Implementation Snapshot

- **Client/server architecture:** one server process runs the simulation tick and broadcasts `MSG_WORLD_STATE` snapshots; terminal and GUI clients both consume the same protocol/grid stream (`src/server/server.c`, `src/shared/protocol.h`).
- **Atomic simulation path:** each tick runs parallel age → parallel CAS spread (8-neighbor, no age-0 cascade) → serial nutrient/scents/combat/turnover/mutation/division/recombination/dynamic-spawn/behavior updates (`src/server/atomic_sim.c`).
- **Strategy archetypes:** genomes are seeded from 8 randomized archetypes (`BERSERKER`, `TURTLE`, `SWARM`, `TOXIC`, `HIVE`, `NOMAD`, `PARASITE`, `CHAOTIC`) in `genome_create_random()` (`src/server/genetics.c`).
- **Scent/quorum/biofilm/dormancy:** scent fields bias spread direction; quorum activation comes from `signal_strength` vs `quorum_threshold`; biofilm is accumulated/decayed each tick; dormancy is stress-triggered and trades expansion for survival (`src/server/atomic_sim.c`, `src/server/simulation.c`).
- **TUI/GUI parity:** both clients support pause/speed/reset/selection over the same server protocol, but GUI adds mouse + zoom/grid/info controls while demo mode exists only in TUI (`src/client/*`, `src/gui/*`).
- **`run.sh` port behavior:** script default is `8765`, `-p/--port` overrides env/default, and server-starting modes auto-stop an existing `ferox_server` on that port but refuse to kill non-ferox listeners (`scripts/run.sh`).

## Project Structure

```
ferox/
├── src/
│   ├── shared/     # Common utilities, types, networking
│   ├── server/     # Simulation engine and server
│   ├── client/     # Terminal visualization client
│   └── gui/        # SDL2 GUI client
├── tests/          # Unit and integration tests
├── docs/           # Documentation
└── cmake/          # CMake modules
```

## Building

### Prerequisites

- CMake 3.16 or higher
- GCC or Clang with C11 support
- POSIX threads (pthreads)

### Quick Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
make -j$(nproc)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTS` | ON | Build unit tests |
| `BUILD_DOCS` | OFF | Build documentation |
| `ENABLE_SANITIZERS` | OFF | Enable address/undefined sanitizers |

Server builds automatically enable SIMD hot-loop kernels when supported (`AVX2` on x86_64 via target attributes, `NEON` on arm64), with scalar fallbacks retained for portability.

```bash
# Example: Build with sanitizers enabled
cmake -DENABLE_SANITIZERS=ON ..

# Example: Release build without tests
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF ..
```

### Running Tests

```bash
# Run all tests
cd build
ctest --output-on-failure

# Run specific test
ctest -R Phase1Tests

# Run with verbose output
ctest -V

# Run SIMD + performance evaluation tests
./scripts/test.sh perf
```

Performance baseline workflow and known bottlenecks are documented in `docs/PERFORMANCE.md`.

### Installation

```bash
# Install to default prefix (/usr/local)
sudo make install

# Install to custom location
cmake -DCMAKE_INSTALL_PREFIX=/opt/ferox ..
make install
```

## Usage

### Using Scripts (Recommended)

```bash
# Build the project
./scripts/build.sh

# Run server + terminal client
./scripts/run.sh

# Run server + GUI client
./scripts/run.sh gui+

# Run terminal client in demo mode (no server)
./scripts/run.sh demo

# Run all tests
./scripts/test.sh
```

### Manual Execution

**Starting the Server:**

```bash
./ferox_server -p 8080 -w 200 -H 100 -c 20 -t 4 -r 50
```

| Option | Description |
|--------|-------------|
| `-p, --port` | TCP port (default: 8080) |
| `-w, --width` | World grid width |
| `-H, --height` | World grid height |
| `-c, --colonies` | Initial colony count |
| `-t, --threads` | Thread pool size |
| `-r, --rate` | Tick rate in ms (default: 50 from `main.c`) |

**Starting Clients:**

```bash
# Terminal client
./ferox_client -h 127.0.0.1 -p 8080

# Terminal client in demo mode
./ferox_client --demo

# GUI client (requires SDL2)
./ferox_gui -h 127.0.0.1 -p 8080
```

> Note: standalone client defaults differ (`ferox_client`: 7890, `ferox_gui`: 7777), so pass `-p` explicitly unless using `scripts/run.sh`.

## Contributing

See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) for development guidelines.

## Security

See [SECURITY.md](SECURITY.md) for security policy and vulnerability reporting.

## Development

### Debug Build

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

### Running Individual Tests

```bash
# Direct execution
./test_phase1

# With CTest
ctest -R Phase1 --output-on-failure
```

## License

See LICENSE file for details.
