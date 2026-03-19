# GitHub Project Hygiene

This runbook describes how to use the Ferox GitHub Project, labels, milestone,
and umbrella issues for day-to-day maintenance.

## Current Tracking Surfaces

- project: `Ferox Research Backlog`
- milestone: `Research Backlog Sweep`
- umbrella issues:
  - `#144` runtime and core performance
  - `#145` scheduler, atomics, and memory infrastructure
  - `#146` network transport and protocol evolution
  - `#147` determinism, replayability, and science validation
  - `#148` ecology and colony behavior

## Required Issue Hygiene

When creating or triaging a new issue:

1. start from the closest issue template
2. add the correct area label(s)
3. add one priority label
4. add `tracking` when it belongs in the managed backlog
5. attach it to the current milestone if it is part of the active sweep
6. add it to the GitHub Project
7. set project fields:
   - `Status`: usually `Todo`
   - `Track`: choose the main workstream
   - `Priority`: `P0`, `P1`, or `P2`
   - `Wave`: `Now`, `Next`, `Later`, or `Meta`
8. link the issue to the right umbrella issue
9. call out required documentation updates in the issue body

## Project Field Meanings

### Status

- `Todo`: not started yet
- `In Progress`: active implementation or active investigation
- `Done`: merged or otherwise completed

### Track

- `Runtime & Perf`: simulation hot paths, scaling, runtime throughput
- `Infra`: scheduler, atomics, memory layout, allocator, infrastructure
- `Network & Protocol`: transport, wire format, client/server update path
- `Replay & Science`: determinism, replay, benchmarks, statistical validation
- `Ecology & Behavior`: simulation model, genetics, colony behavior, realism

### Priority

- `P0`: highest near-term leverage or blocker removal
- `P1`: important follow-on work
- `P2`: useful but not urgent

### Wave

- `Now`: first wave work; strongest near-term candidates
- `Next`: important follow-up after the current blockers / foundations land
- `Later`: lower urgency or higher-risk work
- `Meta`: umbrella / coordination / tracking issues

## Suggested Views

Recommended project views to keep in GitHub:

- board grouped by `Status`
- table sorted by `Wave`, then `Priority`
- filtered views for each `Track`
- filtered view for `priority:p0`
- filtered view for `area:docs` to catch process/doc follow-through

## Triage Rhythm

### New issue intake

- check whether it belongs under an existing umbrella issue
- decide if it should be in `Now`, `Next`, or `Later`
- ensure the issue body includes evidence, validation, and doc expectations

### During implementation

- move `Status` to `In Progress`
- update the umbrella issue checklist or comments if sequencing changes
- add follow-up issues immediately instead of leaving TODOs only in code

### After merge

- move `Status` to `Done`
- close the issue or adjust it if more work remains
- update linked planning docs if the issue changed scope or spawned follow-ons
- keep umbrella issues current so they remain useful dashboards

## Documentation Rule

Ferox treats documentation updates as part of completion.

If an issue changes behavior, metrics, defaults, workflows, wire format, or test
expectations, the matching docs should land in the same PR.

Common docs to update:

- `docs/PERFORMANCE_BACKLOG.md`
- `docs/SCALING_AND_BEHAVIOR_PLAN.md`
- `docs/COLONY_INTELLIGENCE.md`
- `docs/SIMULATION.md`
- `docs/GENETICS.md`
- `docs/PROTOCOL.md`
- `docs/PERFORMANCE.md`
- `docs/PERF_RUNBOOK.md`
- `docs/SCIENCE_BENCHMARKS.md`
- `docs/STATISTICAL_REGRESSION.md`
- `docs/TESTING.md`
- `docs/CONTRIBUTING.md`
- `docs/DEVELOPMENT_CYCLE.md`
- `docs/README.md`
- `README.md`

## Fast Maintainer Checklist

Before starting work:

- issue exists
- labels are correct
- project fields are set
- umbrella link exists
- docs expectation is explicit

Before merging work:

- validation is recorded in the PR
- docs are updated in the same PR
- follow-up issues are filed if needed
- project / issue state is ready to move to `Done`
