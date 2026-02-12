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

## Project Structure

```
ferox/
├── src/
│   ├── shared/     # Common utilities, types, networking
│   ├── server/     # Simulation engine and server
│   └── client/     # Visualization client
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
./ferox_server -p 8765 -w 100 -H 100 -c 10 -t 4
```

| Option | Description |
|--------|-------------|
| `-p, --port` | TCP port (default: 8080) |
| `-w, --width` | World grid width |
| `-H, --height` | World grid height |
| `-c, --colonies` | Initial colony count |
| `-t, --threads` | Thread pool size |

**Starting Clients:**

```bash
# Terminal client
./ferox_client localhost 8765

# Terminal client in demo mode
./ferox_client --demo

# GUI client (requires SDL2)
./ferox_gui localhost 8765
```

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
