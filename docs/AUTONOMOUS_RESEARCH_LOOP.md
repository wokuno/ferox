# Autonomous Research Loop

This workflow lets the autonomous runner execute one Ferox research experiment at a time, print only a short summary, and then continue from the same dedicated session on the next invocation.

If you want to watch the agent as it runs, use `--show-session`; live step markers, tool activity, and assistant text are mirrored to stderr while the final extracted summary still goes to stdout.

## Files

- Prompt: `prompts/autonomous_research_loop.md`
- Runner script: `scripts/opencode_research_loop.sh`
- Ignored runtime state/logs: `artifacts/opencode-research-loop/`

## What the loop does

Each run asks the backend agent to:

1. Read the current codebase, docs, and prior handoff context.
2. Do fresh research across at least 20 topics spanning multiple improvement areas.
3. Turn that research into a candidate idea list.
4. Briefly reason in the notebook about which ideas are most worth testing now.
5. Pick one experiment with one falsifiable hypothesis from that fresh triage, even if it differs from the previous handoff suggestion.
6. Implement only what is needed for that experiment.
7. Run the relevant tests.
8. If tests pass, run the relevant benchmark or repeated measurement 3 times when that fits the experiment.
9. Analyze the outcome.
10. Commit the change if it is a measured improvement.
11. If `PUBLIC_GH_TOKEN` and `PUBLIC_GH_USER` are available, publish committed improvements through a PR and wait for CI to finish.
12. Otherwise revert only the experiment implementation changes while preserving the run notebook and handoff notes.
13. Update documentation, the run notebook, and the next-agent handoff.

The final agent response must end with a machine-parseable summary block. The shell script extracts that block and prints only the summary text to stdout.

## Usage

Run a single experiment cycle:

```bash
./scripts/opencode_research_loop.sh --once
```

Run continuously until interrupted:

```bash
./scripts/opencode_research_loop.sh
```

Run while watching the agent session live:

```bash
./scripts/opencode_research_loop.sh --show-session
```

Run a fixed number of iterations:

```bash
./scripts/opencode_research_loop.sh --iterations 3
```

Start a fresh dedicated session instead of resuming the saved one:

```bash
./scripts/opencode_research_loop.sh --fresh --once
```

Optional overrides:

```bash
./scripts/opencode_research_loop.sh --model provider/model --agent default --once
```

## State and handoff conventions

- The shell loop stores the `opencode` session id plus the latest raw JSON event log under `artifacts/opencode-research-loop/`.
- The prompt requires a run-specific notebook in `docs/lab-notes/`.
- The prompt requires the latest cross-run handoff in `docs/agent-handoffs/latest.md`, but only as advisory context.
- The notebook is expected to capture both the idea list and the brief reasoning that led to the chosen experiment.
- The research and idea list are expected to cover more than performance alone; simulation behavior, colony complexity, scaling, tooling, client experience, dashboard/site improvements, and other meaningful Ferox improvements are in scope.
- Failed experiments are expected to keep notebook and handoff notes available for the next automated run even if the experiment code itself is reverted.

## Notes

- The script is intentionally quiet: stdout is reserved for the extracted summary only.
- `--show-session` mirrors live step markers, tool activity, and assistant text to stderr so you can watch the run without breaking summary-only stdout.
- If the agent reports `Loop Continue: no`, the script stops after printing that summary.
- The script resumes the same dedicated session by default so the next run keeps the prior context.
- The handoff file's `Next Experiment` is a suggestion, not an instruction; every run is expected to re-research and choose independently.
- For a local dashboard that runs the loop and shows the GIF, latest summary, live run state, notebooks, and handoff in the browser, use `go run ./cmd/feroxclub` and see `docs/FEROX_CLUB.md`.
- `go run ./cmd/feroxclub` defaults to the GitHub Copilot CLI SDK backend, with optional OpenCode support via `-backend opencode`.
- The dashboard now also refreshes its served preview GIF after each run when local GIF generation succeeds.
