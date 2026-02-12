# Contributing to Ferox

Thank you for your interest in contributing to Ferox! This document provides guidelines for contributing to the project.

## Table of Contents

- [Development Setup](#development-setup)
- [Code Style](#code-style)
- [Commit Message Format](#commit-message-format)
- [Pull Request Process](#pull-request-process)
- [Project Structure](#project-structure)
- [Testing Requirements](#testing-requirements)

## Development Setup

### Prerequisites

- **C11 compiler**: GCC 7+ or Clang 5+
- **POSIX system**: Linux or macOS
- **Git**: For version control
- **Make**: For build automation (optional)

### Getting Started

```bash
# Clone the repository
git clone https://github.com/your-org/ferox.git
cd ferox

# Build the project
gcc -O2 -pthread -o ferox_server src/server/*.c src/shared/*.c
gcc -O2 -o ferox_client src/client/*.c src/shared/*.c

# Run tests
gcc -o tests/test_phase1 tests/test_phase1.c src/shared/*.c
./tests/test_phase1
```

### Development Tools

Recommended tools:
- **clang-format**: Code formatting
- **clang-tidy**: Static analysis
- **valgrind**: Memory debugging
- **gdb/lldb**: Debugging

## Code Style

### General Guidelines

1. **Indentation**: 4 spaces (no tabs)
2. **Line length**: Maximum 100 characters
3. **Braces**: K&R style (opening brace on same line)
4. **Naming**: `snake_case` for functions and variables

### Naming Conventions

| Element | Convention | Example |
|---------|------------|---------|
| Functions | `module_action_object` | `world_get_cell`, `genome_mutate` |
| Variables | `snake_case` | `colony_count`, `spread_rate` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_COLONIES`, `PROTOCOL_MAGIC` |
| Types | `PascalCase` | `World`, `Colony`, `MessageHeader` |
| Enums | `UPPER_SNAKE_CASE` | `DIR_N`, `MSG_CONNECT` |
| Macros | `UPPER_SNAKE_CASE` | `ASSERT`, `RUN_TEST` |

### Function Style

```c
/**
 * Brief description of function.
 * 
 * @param world The world to operate on
 * @param x X coordinate
 * @param y Y coordinate
 * @return Pointer to cell, or NULL if out of bounds
 */
Cell* world_get_cell(World* world, int x, int y) {
    if (!world || x < 0 || x >= world->width || y < 0 || y >= world->height) {
        return NULL;
    }
    return &world->cells[y * world->width + x];
}
```

### Header File Style

```c
#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>
#include <stdbool.h>

// Forward declarations
typedef struct World World;

// Constants
#define MODULE_MAX_VALUE 100

// Type definitions
typedef struct {
    int field1;
    float field2;
} MyStruct;

// Function declarations
MyStruct* module_create(void);
void module_destroy(MyStruct* obj);
int module_process(MyStruct* obj, int value);

#endif // MODULE_H
```

### Comments

```c
// Single-line comments for brief explanations

/*
 * Multi-line comments for longer explanations
 * spanning multiple lines.
 */

/**
 * Documentation comments for public APIs.
 * 
 * @param param Description
 * @return Description
 */
```

### Code Organization

1. **Headers** at the top (system headers, then project headers)
2. **Constants and macros** next
3. **Static (private) functions** before public functions
4. **Public functions** in declaration order from header

```c
#include "module.h"
#include <stdlib.h>
#include <string.h>

#define INTERNAL_CONSTANT 42

// Private helper function
static int helper_function(int value) {
    return value * 2;
}

// Public function from header
int module_process(MyStruct* obj, int value) {
    if (!obj) return -1;
    return helper_function(value);
}
```

## Commit Message Format

We follow the [Conventional Commits](https://www.conventionalcommits.org/) format:

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Types

| Type | Description |
|------|-------------|
| `feat` | New feature |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `style` | Code style (formatting, no logic change) |
| `refactor` | Code refactoring |
| `perf` | Performance improvement |
| `test` | Adding/updating tests |
| `chore` | Build, CI, dependencies |

### Scopes

| Scope | Description |
|-------|-------------|
| `server` | Server-side changes |
| `client` | Client-side changes |
| `shared` | Shared module changes |
| `protocol` | Network protocol changes |
| `genetics` | Genetics system changes |
| `simulation` | Simulation logic changes |
| `tests` | Test changes |

### Examples

```
feat(genetics): add spread_weights to genome

Add 8-directional spread weights to the Genome structure,
allowing colonies to have directional growth preferences.

Closes #42
```

```
fix(simulation): prevent division by zero in genome_merge

Check for zero cell counts before calculating weighted average
in genome_merge function.

Fixes #55
```

```
docs(api): add API reference documentation

- Document all public functions
- Add usage examples
- Organize by module
```

## Pull Request Process

### Before Submitting

1. **Create a branch**: `git checkout -b feature/my-feature`
2. **Make changes**: Implement your feature or fix
3. **Add tests**: Cover new functionality with tests
4. **Run tests**: Ensure all tests pass
5. **Update docs**: Update relevant documentation

### Branch Naming

| Type | Format | Example |
|------|--------|---------|
| Feature | `feature/<name>` | `feature/directional-spreading` |
| Bug fix | `fix/<name>` | `fix/memory-leak-world` |
| Docs | `docs/<name>` | `docs/api-reference` |
| Refactor | `refactor/<name>` | `refactor/protocol-cleanup` |

### Pull Request Template

```markdown
## Description
Brief description of changes.

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Changes Made
- Change 1
- Change 2
- Change 3

## Testing
- [ ] All existing tests pass
- [ ] Added new tests for changes
- [ ] Tested manually

## Checklist
- [ ] Code follows project style guidelines
- [ ] Self-reviewed my code
- [ ] Updated documentation
- [ ] No new warnings
```

### Review Process

1. **Submit PR**: Open pull request against `main`
2. **CI checks**: Automated tests must pass
3. **Code review**: At least one approval required
4. **Address feedback**: Make requested changes
5. **Merge**: Squash and merge when approved

### Merge Requirements

- All CI checks pass
- At least one approval from maintainer
- No merge conflicts
- Commits squashed into logical units

## Project Structure

```
ferox/
├── src/
│   ├── shared/          # Code shared between client and server
│   │   ├── types.h      # Core data structures
│   │   ├── protocol.h/c # Network protocol
│   │   ├── network.h/c  # Socket abstraction
│   │   ├── colors.h/c   # Color utilities
│   │   ├── names.h/c    # Name generation
│   │   └── utils.h/c    # Random/math utilities
│   ├── server/          # Server implementation
│   │   ├── main.c       # Entry point
│   │   ├── server.h/c   # Server core
│   │   ├── world.h/c    # World management
│   │   ├── simulation.h/c # Simulation logic
│   │   ├── genetics.h/c # Genome operations
│   │   ├── threadpool.h/c # Thread pool
│   │   └── parallel.h/c # Parallel processing
│   └── client/          # Client implementation
│       ├── main.c       # Entry point
│       ├── client.h/c   # Client core
│       ├── renderer.h/c # Terminal rendering
│       └── input.h/c    # Input handling
├── tests/               # Test files
├── docs/                # Documentation
└── README.md            # Project readme
```

### Adding New Files

1. Add `.h` and `.c` files to appropriate directory
2. Update include paths
3. Add to compilation commands
4. Add tests in `tests/`
5. Document public APIs

## Testing Requirements

### For Bug Fixes

- Add a test that reproduces the bug
- Test passes after fix
- No regression in existing tests

### For New Features

- Unit tests for all new functions
- Tests for edge cases (NULL, zero, bounds)
- Integration tests if applicable
- Update stress tests if performance-critical

### Running Tests

```bash
# Compile all tests
for f in tests/*.c; do
    gcc -pthread -o "${f%.c}" "$f" src/**/*.c
done

# Run all tests
for test in tests/test_phase*; do
    echo "Running $test..."
    $test
done
```

### Test Coverage Goals

| Category | Goal |
|----------|------|
| Core utilities | 100% |
| Genetics | 90%+ |
| Simulation | 85%+ |
| Protocol | 100% |
| Server/Client | 80%+ |

## Getting Help

- **Issues**: Open a GitHub issue for bugs or feature requests
- **Discussions**: Use GitHub Discussions for questions
- **Documentation**: Check `docs/` for detailed documentation

## License

By contributing, you agree that your contributions will be licensed under the project's MIT License.

---

Thank you for contributing to Ferox! 🦠
