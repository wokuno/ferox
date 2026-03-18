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
- **Behavior Graph Runtime** - Colonies evaluate a weighted sensing -> drive -> action controller for smarter mode selection
- **Client/Server Architecture** - Multiple clients can connect to observe the same simulation

### Clients
- **Terminal Client** - Beautiful 24-bit color display with box-drawing characters
- **GUI Client (SDL2)** - Grid-based rendering, zoom/pan, colony selection, continuous key input
- **Real-time Controls** - Pause, speed up/down, select colonies for detailed info
- **Selected Colony Stat Sheets** - CLI and GUI show state, mode, action outputs, and character summaries when tabbing through colonies

### Quality
- **Comprehensive Test Suite** - Multi-layer test coverage across correctness, stress, and performance diagnostics

## Quick Start

### Building

```bash
# Configure + build (Release)
./scripts/build.sh release
```

### Running

**Start server + terminal client together:**
```bash
./scripts/run.sh
```

**Start server + GUI client:**
```bash
./scripts/run.sh gui+
```

**Run only server:**
```bash
./build/src/server/ferox_server -p 8765 -w 100 -H 50 -c 10
```

**Connect a client:**
```bash
./build/src/client/ferox_client localhost 8765
```

### Testing

```bash
# Run full CTest matrix
ctest --test-dir build --output-on-failure

# Run focused performance diagnostics
ctest --test-dir build --output-on-failure -R "ThreadpoolMicrobenchTests|ThreadpoolProfileScanTests|PerformanceProfilingTests|PerfUnitWorldTests|PerfUnitProtocolTests|PerfComponentAtomicTests"
```

### Performance Workflow

```bash
# Multi-iteration median summary (recommended)
./scripts/perf_multi_iter.py -n 7 --profile balanced
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
| `-w, --width` | 200 | World grid width |
| `-H, --height` | 100 | World grid height |
| `-c, --colonies` | 20 | Initial colony count |
| `-t, --threads` | detected logical CPUs | Thread pool size |
| `-r, --rate` | 100 | Milliseconds per tick |
| `-a, --accelerator` | `auto` | Runtime target: `auto`, `cpu`, `apple`, or `amd` |

### Hardware Tuning Environment

| Variable | Default | Description |
|----------|---------|-------------|
| `FEROX_ACCELERATOR` | `auto` | Select runtime target when CLI override is not provided |
| `FEROX_THREADPOOL_PROFILE` | auto-tuned | Override threadpool scheduler profile |
| `FEROX_ATOMIC_SERIAL_INTERVAL` | auto-tuned | Override serial maintenance cadence inside `atomic_tick` |
| `FEROX_ATOMIC_FRONTIER_DENSE_PCT` | auto-tuned | Override spread frontier dense cutoff |
| `FEROX_ATOMIC_USE_FRONTIER` | auto-tuned | Force frontier mode on or off |

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
- [API Reference](API.md) - Function documentation
- [Testing](TESTING.md) - Test organization and coverage
- [Performance Runbook](PERF_RUNBOOK.md) - Benchmark commands, profiles, and jitter control
- [Hardware and Accelerators](HARDWARE_ACCELERATION.md) - Host detection, target selection, and tuning defaults
- [Colony Intelligence](COLONY_INTELLIGENCE.md) - Current behavior model, gaps, and future graph vision
- [Performance Targets](PERF_TARGETS.md) - Current median baselines and target thresholds
- [Performance Backlog](PERFORMANCE_BACKLOG.md) - Prioritized optimization roadmap
- [Performance History](PERFORMANCE_HISTORY.md) - Detailed record of changes, experiments, and outcomes
- [Scaling and Behavior Plan](SCALING_AND_BEHAVIOR_PLAN.md) - Current rollout plan and linked GitHub issues
- [Progress](PROGRESS.md) - Current project status and active workstreams
- [Contributing](CONTRIBUTING.md) - Development guidelines
- [Development Cycle](DEVELOPMENT_CYCLE.md) - Issue intake, project tracking, validation, docs updates, and merge flow

## GitHub Project Hygiene

- Use the `Ferox Research Backlog` GitHub Project for the current research sweep.
- Prefer the issue templates under `.github/ISSUE_TEMPLATE/` for new perf,
  protocol, model, and science work so validation and documentation follow-up are
  captured up front.
- Keep linked planning docs updated in the same PR as implementation work.
- Follow `DEVELOPMENT_CYCLE.md` for the standard capture -> triage -> implement ->
  validate -> document -> review -> merge loop.

## License

MIT License - See LICENSE file for details.

## Acknowledgments

Inspired by Conway's Game of Life and bacterial growth simulations.
