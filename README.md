# Ferox - Bacterial Colony Simulator

A multi-threaded bacterial colony simulation with client-server architecture.

## Features

- Real-time bacterial colony simulation
- Genetic mutation and evolution mechanics
- Multi-threaded simulation engine
- Client-server architecture for remote viewing
- Thread pool for parallel processing

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

### Starting the Server

```bash
./ferox_server [options]
```

### Starting the Client

```bash
./ferox_client [server_address] [port]
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
