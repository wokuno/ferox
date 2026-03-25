# ferox.club Local Dashboard

This Go program runs the autonomous research loop and serves a local dashboard for watching Ferox research evolve in real time.

By default it uses the GitHub Copilot CLI SDK. It can also drive a local OpenCode server through the canonical Go module `github.com/sst/opencode-sdk-go`.

## What it shows

- the Ferox preview GIF from `assets/preview.gif`
- the most recent run summary extracted from the autonomous prompt
- a live dashboard while a run is active, including:
  - current run status
  - session id
  - recent step and tool events
  - live assistant transcript
- browsable lab notebooks and the latest handoff note
- the active backend (`copilot` or `opencode`)

## Run it

From the repo root:

```bash
go run ./cmd/feroxclub
```

Then open:

```bash
http://localhost:8787
```

## Use `ferox.club` locally

Add a hosts entry:

```bash
echo "127.0.0.1 ferox.club" | sudo tee -a /etc/hosts
```

Then run the dashboard and open:

```bash
http://ferox.club:8787
```

## Useful flags

```bash
go run ./cmd/feroxclub --help
```

Common examples:

```bash
# Single run only
go run ./cmd/feroxclub -once

# Use the default Copilot backend explicitly
go run ./cmd/feroxclub -backend copilot

# Use OpenCode through a locally started headless server
go run ./cmd/feroxclub -backend opencode

# Delay 30 seconds between runs
go run ./cmd/feroxclub -delay 30s

# Fresh backend session
go run ./cmd/feroxclub -fresh

# Bind to a different port
go run ./cmd/feroxclub -addr 127.0.0.1:9090

# Reuse an existing OpenCode server instead of auto-starting one
go run ./cmd/feroxclub -backend opencode -opencode-base-url http://127.0.0.1:4096
```

## State

- loop state and logs are stored under `artifacts/ferox-club/`
- the prompt comes from `prompts/autonomous_research_loop.md`
- the dashboard reads and updates the latest summary automatically
- session reuse is tracked per backend with files like `artifacts/ferox-club/copilot_session_id` and `artifacts/ferox-club/opencode_session_id`
- the locally served GIF can refresh on each completed run via `artifacts/ferox-club/live-preview.gif`

## Notes

- this runner is separate from `scripts/opencode_research_loop.sh`
- the dashboard uses polling against `/api/state` to keep the page updated during active runs
- the OpenCode support uses the canonical module path `github.com/sst/opencode-sdk-go`; the older `github.com/anomalyco/opencode-sdk-go` import path is not valid for `go get`
- committed autonomous runs can be published as PRs through `github.com/google/go-github/v69/github` when `PUBLIC_GH_TOKEN` and `PUBLIC_GH_USER` are present
