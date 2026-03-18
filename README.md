# Ferox - Bacterial Colony Simulator

A multi-threaded bacterial colony simulation with client-server architecture.

## Features

- Real-time bacterial colony simulation
- Genetic mutation and evolution mechanics
- Multi-threaded simulation engine
- Accelerator-aware runtime tuning for CPU, Apple Silicon, and AMD GPU hosts
- Client-server architecture for remote viewing
- Thread pool for parallel processing

## Project Structure

```
ferox/
├── src/
│   ├── shared/     # Common utilities, types, networking
│   ├── server/     # Simulation engine and server
│   ├── client/     # Terminal visualization client
│   └── gui/        # SDL-based GUI client
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
```

### Installation

```bash
# Install to default prefix (/usr/local)
sudo make install

# Install to custom location
cmake -DCMAKE_INSTALL_PREFIX=/opt/ferox ..
make install
```

## Usage

### Starting the Server

```bash
./ferox_server --print-hardware
./ferox_server -w 400 -H 200 -c 50 --accelerator auto
```

Inspect detected hardware and runtime target:

```bash
./ferox_server --print-hardware
```

### Starting the Client

```bash
./ferox_client -h localhost -p 8080
```

## Contributing

See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) for development guidelines.
See [docs/DEVELOPMENT_CYCLE.md](docs/DEVELOPMENT_CYCLE.md) for the issue-driven
development workflow, GitHub Project usage, validation expectations, and the
rule that documentation updates land in the same PR as behavior/workflow changes.

## Hardware Support

- Ferox auto-detects CPU-only, Apple Silicon, and AMD GPU hosts.
- The current execution backend remains the lock-free CPU atomic simulation path.
- Apple and AMD targets currently control host-side tuning defaults and runtime reporting.
- See [docs/HARDWARE_ACCELERATION.md](docs/HARDWARE_ACCELERATION.md) for details.

## Current Defaults

- server world: `400x200`
- initial colonies: `50`
- tick rate: `100 ms`
- threads: auto-detected logical CPUs unless `-t/--threads` is provided
- see [docs/SCALING_AND_BEHAVIOR_PLAN.md](docs/SCALING_AND_BEHAVIOR_PLAN.md) for the rollout plan and tracked issues

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
