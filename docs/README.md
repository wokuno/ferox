# Ferox - Bacterial Colony Simulator

**Ferox** is a real-time bacterial colony simulation featuring evolving genomes, territorial competition, and networked multi-client visualization. Watch as colonies spread, mutate, divide, and recombine in a colorful terminal-based display.

```
┌──────────────────────────────────────────────────────────────────┐
│                    Ferox Colony Simulator                        │
├──────────────────────────────────────────────────────────────────┤
│  ●●●●●                   ○○○○○○○○○○                              │
│  ●●●●●●●                ○○○○○○○○○○○○                             │
│  ●●●●●●●●●             ○○○○○○○○○○○○○○                            │
│   ●●●●●●●●●           ○○○○○○○○○○○○○○○                            │
│    ●●●●●●●●●●        ○○○○○○○○○○○○○○                              │
│     ●●●●●●●●●●●     ○○○○○○○○○○○○○                                │
│      ●●●●●●●●●●●●  ○○○○○○○○○○○○         ▪▪▪▪▪▪                   │
│       ●●●●●●●●●●●●○○○○○○○○○○          ▪▪▪▪▪▪▪▪▪                  │
│        ●●●●●●●●●●●○○○○○○○○          ▪▪▪▪▪▪▪▪▪▪▪                  │
│         ●●●●●●●●●●●●○○○            ▪▪▪▪▪▪▪▪▪▪▪▪                  │
│          ●●●●●●●●●●●●            ▪▪▪▪▪▪▪▪▪▪▪▪▪▪                  │
│           ●●●●●●●●●●            ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪                  │
├──────────────────────────────────────────────────────────────────┤
│ Tick: 1247 | Colonies: 12 | Speed: 1.0x | [RUNNING]              │
│ Selected: Bacillus feroxii | Cells: 2,341 | Age: 892             │
└──────────────────────────────────────────────────────────────────┘
```

## Features

### Core Simulation
- **Genetic Evolution** - Each colony has a genome controlling spread patterns, mutation rates, aggression, resilience, and colors
- **Social Behavior (Chemotaxis)** - Colonies detect nearby neighbors and exhibit attraction or repulsion based on social_factor genes
- **Family-Based Recombination** - Only related colonies (parent-child or siblings) can merge, creating stable ecosystems
- **Cascade-Free Growth** - Newly claimed cells wait one tick before spreading, ensuring smooth realistic expansion
- **Organic Colony Borders** - Cell-based rendering shows actual territory with distinct border colors
- **Territorial Competition** - Colonies compete for space based on aggression vs. resilience mechanics
- **Colony Lifecycle** - Colonies track their peak population history and die when population reaches 0
- **Colony Division** - When colonies split geographically, they divide into separate species with mutated genomes

### Environmental Systems
- **Nutrient Layer** - Spatial nutrient distribution affecting colony growth and spread direction
- **Toxin Layer** - Colonies produce toxins that damage nearby competitors
- **Chemical Signals** - Colony communication through signal emission and detection
- **Colony States** - NORMAL, DORMANT, and STRESSED states with adaptive behavior

### Advanced Genetics
- **Environmental Sensing** - nutrient_sensitivity, edge_affinity, density_tolerance traits
- **Colony Interactions** - toxin production/resistance, signal emission/sensitivity, gene transfer
- **Survival Strategies** - dormancy threshold/resistance, biofilm investment, motility
- **Metabolic Efficiency** - Resource conversion optimization

### Technical Features
- **Lock-Free Parallel Simulation** - Atomic CAS-based spreading enables 28%+ CPU utilization with no lock contention
- **GPU/MPI/SHMEM Ready** - Architecture designed for CUDA, OpenCL, and distributed computing acceleration
- **Client/Server Architecture** - Multiple clients can connect to observe the same simulation

### Clients
- **Terminal Client** - Beautiful 24-bit color display with box-drawing characters
- **GUI Client (SDL2)** - Grid-based rendering, zoom/pan, colony selection, continuous key input
- **Real-time Controls** - Pause, speed up/down, select colonies for detailed info

### Quality
- **Comprehensive Test Suite** - 15 test suites with 200+ tests ensuring simulation correctness

## Quick Start

### Building

```bash
# Using CMake (recommended)
cd ferox
mkdir build && cd build
cmake ..
make -j$(nproc)

# Or use the build script
./scripts/build.sh
```

### Running

**Using scripts (recommended):**
```bash
./scripts/run.sh              # Server + terminal client
./scripts/run.sh gui+         # Server + GUI client
./scripts/run.sh demo         # Demo mode (no server)
```

**Manual execution:**
```bash
# Start the server
./ferox_server -p 8765 -w 100 -H 50 -c 10

# Connect terminal client
./ferox_client localhost 8765

# Connect GUI client (requires SDL2)
./ferox_gui localhost 8765

# Run demo mode (no server needed)
./ferox_client --demo
```

### Keyboard Controls

| Key | Action |
|-----|--------|
| `q` | Quit client |
| `p` | Pause/Resume simulation |
| `+` / `=` | Speed up simulation |
| `-` | Slow down simulation |
| `↑` `↓` `←` `→` | Scroll viewport |
| `Enter` | Select colony under cursor |
| `Esc` | Deselect colony |
| `r` | Reset simulation |

## Configuration Options

### Server Options

Server binary defaults shown (scripts/run.sh may use different defaults):

| Option | Default | Description |
|--------|---------|-------------|
| `-p, --port` | 8080 | TCP port to listen on (0 for auto-assign) |
| `-w, --width` | 100 | World grid width |
| `-H, --height` | 100 | World grid height |
| `-c, --colonies` | 5 | Initial colony count |
| `-t, --threads` | 4 | Thread pool size |
| `-r, --rate` | 100 | Milliseconds per tick |
| `--nutrient-diffusion` | 0.00 | Nutrient diffusion coefficient (`0.0-0.25`) |
| `--nutrient-decay` | 0.00 | Nutrient decay coefficient (`0.0-1.0`) |
| `--toxin-diffusion` | 0.00 | Toxin diffusion coefficient (`0.0-0.25`) |
| `--toxin-decay` | 0.05 | Toxin decay coefficient (`0.0-1.0`) |
| `--signal-diffusion` | 0.075 | Signal diffusion coefficient (`0.0-0.25`) |
| `--signal-decay` | 0.10 | Signal decay coefficient (`0.0-1.0`) |

Solver stability guardrail per field: `4 * diffusion + decay <= 1.0`.

### Client Options

| Option | Default | Description |
|--------|---------|-------------|
| `host` | (required) | Server hostname (positional) |
| `port` | (required) | Server port (positional) |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                          FEROX SYSTEM                           │
├───────────────────────────────┬─────────────────────────────────┤
│            SERVER             │           CLIENT(S)             │
│  ┌─────────────────────────┐  │  ┌───────────────────────────┐  │
│  │    Simulation Loop      │  │  │        Renderer           │  │
│  │  ┌───────────────────┐  │  │  │  ┌─────────────────────┐  │  │
│  │  │    Thread Pool    │  │  │  │  │  Terminal Display   │  │  │
│  │  │  ┌────┐┌────┐┌────┐│  │  │  │  │  (24-bit color)     │  │  │
│  │  │  │ T1 ││ T2 ││ T3 ││  │  │  │  └─────────────────────┘  │  │
│  │  │  └────┘└────┘└────┘│  │  │  ├───────────────────────────┤  │
│  │  └───────────────────┘  │  │  │       Input Handler        │  │
│  ├─────────────────────────┤  │  │  ┌─────────────────────┐  │  │
│  │          World          │  │  │  │   Keyboard Input    │  │  │
│  │  ┌───────────────────┐  │  │  │  └─────────────────────┘  │  │
│  │  │    Grid Cells     │  │  │  └───────────────────────────┘  │
│  │  │    Colonies       │  │  │                                 │
│  │  │    Genomes        │  │  │                                 │
│  │  └───────────────────┘  │  │                                 │
│  └─────────────────────────┘  │                                 │
│             ▲                 │               ▲                 │
│             │                 │               │                 │
│             ▼                 │               ▼                 │
│  ┌─────────────────────────┐  │  ┌───────────────────────────┐  │
│  │    Network Server       │◄─┼──┤      Network Client       │  │
│  │    (TCP Socket)         │──┼─►│      (TCP Socket)         │  │
│  └─────────────────────────┘  │  └───────────────────────────┘  │
└───────────────────────────────┴─────────────────────────────────┘

Protocol: Binary messages with 14-byte headers
          World state broadcasts + command responses
```

## Dependencies

- **C11 compiler** (GCC or Clang) with C11 atomics support
- **POSIX threads** (pthreads)
- **POSIX sockets** (BSD sockets)
- **Terminal with 24-bit color support** (most modern terminals)

### Platform Support

| Platform | Status |
|----------|--------|
| Linux | ✅ Fully supported |
| macOS | ✅ Fully supported |
| Windows (WSL) | ✅ Works in WSL |
| Windows (native) | ❌ Not supported |

## Project Structure

```
ferox/
├── src/
│   ├── shared/          # Shared code between client and server
│   │   ├── types.h      # Core data structures (Cell, Colony, Genome, World)
│   │   ├── atomic_types.h # Atomic types for lock-free parallel processing
│   │   ├── protocol.h/c # Network protocol serialization
│   │   ├── network.h/c  # Socket abstraction layer
│   │   ├── colors.h/c   # Color utilities (HSV, blending)
│   │   ├── names.h/c    # Scientific name generator
│   │   └── utils.h/c    # Random number utilities
│   ├── server/          # Server implementation
│   │   ├── main.c       # Server entry point
│   │   ├── server.h/c   # Server core (client management, broadcasting)
│   │   ├── world.h/c    # World state management
│   │   ├── simulation.h/c # Simulation tick logic
│   │   ├── atomic_sim.h/c # Lock-free atomic simulation engine
│   │   ├── genetics.h/c # Genome operations
│   │   ├── threadpool.h/c # Thread pool implementation
│   │   └── parallel.h/c # Parallel update orchestration
│   └── client/          # Client implementation
│       ├── main.c       # Client entry point
│       ├── client.h/c   # Client core (network, state)
│       ├── renderer.h/c # Terminal rendering
│       └── input.h/c    # Keyboard input handling
├── tests/               # Unit and integration tests
├── docs/                # Documentation
└── ferox_server         # Server binary
└── ferox_client         # Client binary
```

## Documentation

- [Architecture](ARCHITECTURE.md) - System design and data flow
- [Protocol](PROTOCOL.md) - Network protocol specification
- [Genetics](GENETICS.md) - Genome structure and evolution
- [Simulation](SIMULATION.md) - World update mechanics
- [Science Benchmarks](SCIENCE_BENCHMARKS.md) - Canonical validation scenarios and pass bands
- [API Reference](API.md) - Function documentation
- [Science Bibliography](SCIENCE_BIBLIOGRAPHY.md) - DOI-backed research mapped to Ferox mechanics
- [Testing](TESTING.md) - Test organization and coverage
- [Statistical Regression](STATISTICAL_REGRESSION.md) - Multi-seed simulation drift checks and CI thresholds
- [Contributing](CONTRIBUTING.md) - Development guidelines
- [Self-Hosted Runner Ops](RUNNER_OPS.md) - Operations and incident response for CI runner

## License

MIT License - See LICENSE file for details.

## Acknowledgments

Inspired by Conway's Game of Life and bacterial growth simulations.
