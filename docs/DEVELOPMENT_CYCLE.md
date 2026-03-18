# Ferox Development Cycle

This document describes the working development loop for Ferox: how ideas enter
the backlog, how implementation work is tracked, how validation is expected to
run, and how documentation stays in sync with code.

## Goals

- keep research, implementation, and follow-up work visible in GitHub
- make issue scope, validation, and documentation expectations explicit up front
- avoid landing behavior or workflow changes without matching docs updates
- keep performance, protocol, and simulation work tied to measurable evidence

## GitHub Operating Model

Ferox currently uses four layers of GitHub tracking:

1. **Issue templates**
   - new work should start from the matching template in
     `.github/ISSUE_TEMPLATE/`
   - templates capture the problem, evidence, proposed direction, validation
     plan, and required documentation updates
2. **Labels**
   - area labels: `area:perf`, `area:threading`, `area:network`,
     `area:protocol`, `area:model`, `area:science`, `area:repro`, `area:docs`
   - priority labels: `priority:p0`, `priority:p1`, `priority:p2`
   - tracking label: `tracking`
3. **Milestone**
   - current research sweep milestone: `Research Backlog Sweep`
4. **GitHub Project**
   - project: `Ferox Research Backlog`
   - custom fields: `Track`, `Priority`, and built-in `Status`

## Umbrella Issues

Large workstreams are grouped under umbrella issues so related tasks can evolve
without losing the bigger picture.

- `#144` simulation runtime and core performance
- `#145` scheduler, atomics, and memory infrastructure
- `#146` network transport and protocol evolution
- `#147` determinism, replayability, and science validation
- `#148` ecology and colony behavior

Child issues should link back to their umbrella issue, and umbrella issues should
be updated when sequencing, scope, or dependencies change materially.

## Standard Development Cycle

### 1. Capture

- start with a GitHub issue, not an untracked todo
- choose the closest issue template
- include concrete evidence: file references, docs references, tests, profiles,
  benchmark output, or previous issues
- call out the narrow first slice rather than a vague long-term ambition

### 2. Triage

- add the correct area and priority labels
- attach the issue to the `Research Backlog Sweep` milestone when relevant
- add the issue to the `Ferox Research Backlog` project
- set `Track`, `Priority`, and initial `Status=Todo`
- link the issue to its umbrella/meta issue if it belongs to an existing stream

### 3. Prepare

- create a focused branch from `main`
- confirm the affected docs before changing code
- identify the validation ladder for the work:
  - correctness tests
  - perf/component tests
  - protocol fixtures
  - science/regression scenarios

Recommended branch naming:

- `feature/<short-name>`
- `fix/<short-name>`
- `perf/<short-name>`
- `docs/<short-name>`
- `refactor/<short-name>`

### 4. Implement

- keep the first PR narrow and reversible
- avoid mixing unrelated cleanup into the same change
- when touching hot paths or model behavior, preserve a way to compare before vs
  after behavior
- if the issue changes defaults, flags, message formats, or workflows, update the
  docs in the same branch before opening the PR

### 5. Validate

- run the tests that match the issue scope
- for performance work, prefer repeated-run medians over one-off numbers
- for protocol work, validate both code and documentation parity
- for model/science work, validate both correctness and outcome behavior

Typical validation references:

- `docs/TESTING.md`
- `docs/PERF_RUNBOOK.md`
- `docs/SCIENCE_BENCHMARKS.md`
- `docs/STATISTICAL_REGRESSION.md`

### 6. Document

Documentation updates are part of the change, not follow-up chores.

At minimum, update the relevant planning and behavior docs when they change:

- `docs/PERFORMANCE_BACKLOG.md`
- `docs/PERFORMANCE.md`
- `docs/SIMULATION.md`
- `docs/GENETICS.md`
- `docs/PROTOCOL.md`
- `docs/ARCHITECTURE.md`
- `docs/PERF_RUNBOOK.md`
- `docs/TESTING.md`
- `docs/SCIENCE_BENCHMARKS.md`
- `docs/STATISTICAL_REGRESSION.md`
- `docs/SCALING_AND_BEHAVIOR_PLAN.md`
- `docs/COLONY_INTELLIGENCE.md`
- `docs/README.md`
- `README.md`

### 7. Open the PR

- use the PR template in `.github/PULL_REQUEST_TEMPLATE.md`
- link the issue with `Closes #...` or `Refs #...`
- summarize the change, validation performed, and docs updated
- note any follow-up issues intentionally left out of scope

### 8. Review and Merge

- request review and wait for required checks
- address review feedback on the latest commit
- re-run the necessary gates after significant follow-up changes
- squash into logical commits if needed before merge

### 9. Close the Loop

After merge:

- close or update the issue state
- update the project item status
- update umbrella issue checklists if scope is now complete or split further
- create follow-up issues immediately if the merged work revealed new gaps

## Definition of Done

An issue is not done just because code landed. It is done when:

- the scoped code change is merged
- matching tests/benchmarks/validation landed or were updated
- relevant documentation was updated in the same PR
- linked issue/project state is current
- any intentional follow-up work has its own issue

## Development Rhythm

The preferred rhythm for Ferox work is:

1. research or observe a problem
2. capture it in GitHub with evidence
3. implement a narrow slice
4. validate with the right test ladder
5. update docs in the same PR
6. merge and immediately re-triage follow-ons

This keeps the codebase, the docs, and the GitHub project moving together.
