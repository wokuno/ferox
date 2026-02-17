# Ferox Refactoring Plan

A comprehensive plan to improve code quality, fix critical issues, and enhance maintainability of the Ferox bacterial colony simulator.

## Table of Contents
1. [Critical Issues (High Priority)](#phase-1-critical-thread-safety-issues)
2. [Memory & Resource Management](#phase-2-memoryresource-management)
3. [Error Handling](#phase-3-error-handling)
4. [Code Duplication](#phase-4-code-duplication-resolution)
5. [Performance](#phase-5-performance-optimizations)
6. [Code Organization](#phase-6-code-organization)
7. [Naming Conventions](#phase-7-naming--consistency)
8. [Edge Cases](#phase-8-edge-case-handling)
9. [Documentation](#phase-9-documentation)

---

## Phase 1: Critical Thread Safety Issues

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 1.1 | `src/server/world.c:216` | Static `next_id` not thread-safe | Move to World struct, use atomic_uint |
| 1.2 | `src/server/server.c:226-240` | No mutex for `server->running` flag | Add pthread_mutex around flag access |
| 1.3 | `src/shared/protocol.c:475` | Static `sequence` counter not thread-safe | Use atomic_uint or add mutex |
| 1.4 | `src/shared/utils.c:9-10` | RNG seeded with `time(NULL)` | Use `clock_gettime` for better seeding |
| 1.5 | `src/server/parallel.c:158-169` | Task allocation without locks | Add mutex or use thread-local allocation |

---

## Phase 2: Memory/Resource Management

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 2.1 | `src/server/server.c:457-462` | CMD_RESET doesn't recreate atomic_world/parallel_ctx | Add recreation logic after world_destroy |
| 2.2 | `src/client/client.c:31` | `proto_world_init()` not called | Call proto_world_init() after memset |
| 2.3 | `src/gui/gui_client.c:124-129` | Double-free potential | Restructure to avoid early proto_world_free |
| 2.4 | `src/server/simulation.c:1286-1312` | Dead buffer reallocation failure leak | Properly cleanup on early return |

---

## Phase 3: Error Handling

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 3.1 | `src/server/world.c:44-51` | Multiple allocations without checks | Add proper error propagation |
| 3.2 | `src/client/client.c:64,79` | protocol_send_message return unchecked | Check return values, handle errors |
| 3.3 | `src/server/world.c:36-41` | Redundant cell initialization | Remove redundant loop (calloc zeros) |
| 3.4 | `src/server/parallel.c:158-209` | Silent allocation failures | Add error logging |

---

## Phase 4: Code Duplication Resolution

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 4.1 | `src/server/simulation.c` vs `atomic_sim.c` | Duplicate nutrient/combat/mutation logic | Create shared functions in `src/shared/simulation_common.c` |
| 4.2 | `src/client/renderer.c:212-323` | Duplicate rendering paths | Consolidate into single function with flags |

---

## Phase 5: Performance Optimizations

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 5.1 | `src/server/world.c:253-258` | O(width*height) loop in world_remove_colony | Use O(active_cells) by tracking cell list per colony |
| 5.2 | `src/server/server.c:293-331` | O(colony_count * grid_size) centroid calc | Use running average or incremental update |
| 5.3 | `src/server/simulation.c:732` | Hardcoded queue[400] | Make dynamic or use linked list |

---

## Phase 6: Code Organization

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 6.1 | `src/server/simulation.c` (~2000 lines) | Too large | Split into: environment.c, combat.c, genetics_wrapper.c |
| 6.2 | `src/server/server.c` | Mixed concerns | Extract client handling to client_manager.c |
| 6.3 | `src/shared/types.h` | 40+ fields in Genome | Group into substructures: CombatStats, GrowthStats, etc. |

---

## Phase 7: Naming & Consistency

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 7.1 | `src/server/server.h:24-30` | ClientSession vs snake_case | Rename to `client_session` |
| 7.2 | `src/shared/network.h:9-21` | NetSocket/NetServer vs net_ functions | Standardize to `net_socket`, `net_server` |
| 7.3 | `src/shared/protocol.h:49-68` | ProtoColony prefix inconsistency | Standardize naming |

---

## Phase 8: Edge Case Handling

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 8.1 | `src/server/genetics.c:212-218` | uint8_t overflow in color gen | Add bounds checking before cast |
| 8.2 | `src/server/world.c:193-237` | No colony ID overflow check | Add UINT32_MAX check |
| 8.3 | `src/shared/colors.c:64` | Integer division precision loss | Use float division |
| 8.4 | `src/server/simulation.c:147-153` | Edge cell boundary check | Fix perception modifier boundary handling |

---

## Phase 9: Documentation

| Task | Location | Issue | Fix |
|------|----------|-------|-----|
| 9.1 | `src/server/atomic_sim.c` | Lock-free algorithm undocumented | Add detailed comments explaining CAS logic |
| 9.2 | `src/server/genetics.c` | 8 strategy archetypes undocumented | Add header comments explaining each archetype |
| 9.3 | `src/server/simulation.c:544-598` | Spread probability calc complex | Document the probability formula |

---

## Agent Assignment Summary

| Agent | Focus Area | Estimated Tasks |
|-------|------------|-----------------|
| Agent 1 | Thread Safety | 5 tasks |
| Agent 2 | Memory/Resources | 4 tasks |
| Agent 3 | Error Handling | 4 tasks |
| Agent 4 | Code Duplication | 2 tasks |
| Agent 5 | Performance | 3 tasks |
| Agent 6 | Code Organization | 3 tasks |
| Agent 7 | Naming Standards | 3 tasks |
| Agent 8 | Edge Cases | 4 tasks |
| Agent 9 | Documentation | 3 tasks |

---

## Recommended Execution Order

1. **Weeks 1-2**: Agent 1 (Thread Safety) - These are most likely to cause crashes
2. **Week 3**: Agent 2 (Memory) - Prevents leaks and crashes  
3. **Week 4**: Agent 3 (Error Handling) - Improves robustness
4. **Weeks 5-6**: Agents 4-5 (Duplication/Performance) - Quality improvements
5. **Weeks 7-8**: Agents 6-9 (Refactoring, naming, docs) - Maintenance
