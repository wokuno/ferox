#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PROMPT_FILE="${OPENCODE_RESEARCH_PROMPT:-$PROJECT_DIR/prompts/autonomous_research_loop.md}"
STATE_DIR="${OPENCODE_RESEARCH_STATE_DIR:-$PROJECT_DIR/artifacts/opencode-research-loop}"
SESSION_FILE="$STATE_DIR/session_id"
LATEST_SUMMARY_FILE="$STATE_DIR/latest-summary.txt"
LATEST_LOG_FILE="$STATE_DIR/latest-run.jsonl"
LATEST_PARSED_FILE="$STATE_DIR/latest-run.parsed.json"
MODEL="${OPENCODE_MODEL:-}"
AGENT="${OPENCODE_AGENT:-}"
ITERATIONS=0
DELAY_SECONDS=0
FRESH=0
SHOW_SESSION=0

usage() {
    cat <<EOF
Usage: ./scripts/opencode_research_loop.sh [options]

Runs the autonomous Ferox research prompt through opencode repeatedly.
Only the extracted per-run summary is printed to stdout.

Options:
  --once                Run a single iteration and exit
  --iterations N        Run exactly N iterations (default: 0, meaning forever)
  --delay-seconds N     Sleep N seconds between iterations
  --prompt PATH         Prompt file to send to opencode
  --model MODEL         Optional opencode model override
  --agent AGENT         Optional opencode agent override
  --fresh               Start a new dedicated session for this loop
  --show-session        Show live tool activity and assistant text on stderr while preserving summary stdout
  -h, --help            Show this help text
EOF
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

validate_integer() {
    case "$2" in
        ''|*[!0-9]*)
            echo "Invalid value for $1: $2" >&2
            exit 1
            ;;
    esac
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --once)
                ITERATIONS=1
                shift
                ;;
            --iterations)
                [[ $# -ge 2 ]] || { echo "Missing value for --iterations" >&2; exit 1; }
                validate_integer "--iterations" "$2"
                ITERATIONS="$2"
                shift 2
                ;;
            --delay-seconds)
                [[ $# -ge 2 ]] || { echo "Missing value for --delay-seconds" >&2; exit 1; }
                validate_integer "--delay-seconds" "$2"
                DELAY_SECONDS="$2"
                shift 2
                ;;
            --prompt)
                [[ $# -ge 2 ]] || { echo "Missing value for --prompt" >&2; exit 1; }
                PROMPT_FILE="$2"
                shift 2
                ;;
            --model)
                [[ $# -ge 2 ]] || { echo "Missing value for --model" >&2; exit 1; }
                MODEL="$2"
                shift 2
                ;;
            --agent)
                [[ $# -ge 2 ]] || { echo "Missing value for --agent" >&2; exit 1; }
                AGENT="$2"
                shift 2
                ;;
            --fresh)
                FRESH=1
                shift
                ;;
            --show-session|--stream)
                SHOW_SESSION=1
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                echo "Unknown option: $1" >&2
                usage >&2
                exit 1
                ;;
        esac
    done
}

extract_summary() {
    local log_file="$1"
    local parsed_file="$2"

    python3 - "$log_file" "$parsed_file" <<'PY'
import json
import re
import sys
from pathlib import Path

log_path = Path(sys.argv[1])
parsed_path = Path(sys.argv[2])

session_id = ""
texts = []
with log_path.open("r", encoding="utf-8") as handle:
    for raw_line in handle:
        raw_line = raw_line.strip()
        if not raw_line:
            continue
        try:
            event = json.loads(raw_line)
        except json.JSONDecodeError:
            continue
        if not session_id and isinstance(event.get("sessionID"), str):
            session_id = event["sessionID"]
        if event.get("type") == "text":
            part = event.get("part") or {}
            text = part.get("text")
            if isinstance(text, str):
                texts.append(text)

full_text = "\n".join(texts)
match = re.search(
    r"AUTONOMOUS_RUN_SUMMARY_BEGIN\s*(.*?)\s*AUTONOMOUS_RUN_SUMMARY_END",
    full_text,
    re.DOTALL,
)
if not match:
    raise SystemExit("Missing autonomous summary markers")

summary = match.group(1).strip()
loop_continue = "yes"
for line in summary.splitlines():
    if line.lower().startswith("loop continue:"):
        value = line.split(":", 1)[1].strip().lower()
        if value in {"yes", "no"}:
            loop_continue = value
        break

payload = {
    "session_id": session_id,
    "summary": summary,
    "loop_continue": loop_continue,
}
parsed_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PY
}

json_field() {
    local parsed_file="$1"
    local field_name="$2"

    python3 - "$parsed_file" "$field_name" <<'PY'
import json
import sys

parsed_path = sys.argv[1]
field_name = sys.argv[2]
with open(parsed_path, "r", encoding="utf-8") as handle:
    payload = json.load(handle)
value = payload.get(field_name, "")
if isinstance(value, str):
    sys.stdout.write(value)
PY
}

capture_run_output() {
    local log_file="$1"

    python3 -c '
import json
import re
import sys
from pathlib import Path

log_path = Path(sys.argv[1])
show_session = sys.argv[2] == "1"

SUMMARY_BEGIN = "AUTONOMOUS_RUN_SUMMARY_BEGIN"
SUMMARY_END = "AUTONOMOUS_RUN_SUMMARY_END"
in_summary_block = False
session_announced = False


def truncate(text, limit=120):
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) <= limit:
        return text
    return text[: limit - 3] + "..."


def emit_line(prefix, text):
    if not text:
        return
    sys.stderr.write(f"{prefix}{text}\n")
    sys.stderr.flush()


def summarize_tool_input(tool_name, state):
    input_data = state.get("input")
    if not isinstance(input_data, dict):
        return ""

    if tool_name == "bash":
        return truncate(input_data.get("description") or input_data.get("command") or "")
    if tool_name == "read":
        return truncate(input_data.get("filePath") or "")
    if tool_name == "glob":
        return truncate(input_data.get("pattern") or "")
    if tool_name == "grep":
        pattern = input_data.get("pattern") or ""
        include = input_data.get("include") or ""
        if include:
            return truncate(f"{pattern} [{include}]")
        return truncate(pattern)
    if tool_name == "question":
        questions = input_data.get("questions")
        if isinstance(questions, list) and questions:
            first = questions[0]
            if isinstance(first, dict):
                return truncate(first.get("header") or first.get("question") or "")

    try:
        return truncate(json.dumps(input_data, sort_keys=True))
    except Exception:
        return ""


def visible_text(text):
    global in_summary_block
    output = []
    remaining = text

    while remaining:
        if in_summary_block:
            end_index = remaining.find(SUMMARY_END)
            if end_index == -1:
                remaining = ""
            else:
                remaining = remaining[end_index + len(SUMMARY_END):]
                in_summary_block = False
        else:
            begin_index = remaining.find(SUMMARY_BEGIN)
            if begin_index == -1:
                output.append(remaining)
                remaining = ""
            else:
                output.append(remaining[:begin_index])
                remaining = remaining[begin_index + len(SUMMARY_BEGIN):]
                in_summary_block = True

    return "".join(output)


def emit_text_block(text):
    cleaned = visible_text(text)
    if not cleaned.strip():
        return
    if not cleaned.endswith("\n"):
        cleaned += "\n"
    sys.stderr.write(cleaned)
    sys.stderr.flush()

with log_path.open("w", encoding="utf-8") as log_handle:
    for raw_line in sys.stdin:
        log_handle.write(raw_line)
        log_handle.flush()

        if not show_session:
            continue

        try:
            event = json.loads(raw_line)
        except json.JSONDecodeError:
            continue

        if not session_announced and isinstance(event.get("sessionID"), str):
            emit_line("[session] ", event["sessionID"])
            session_announced = True

        event_type = event.get("type")
        part = event.get("part") or {}

        if event_type == "step_start":
            emit_line("[step] ", "assistant step started")
            continue

        if event_type == "step_finish":
            reason = part.get("reason") or "finished"
            emit_line("[step] ", f"assistant step finished ({reason})")
            continue

        if event_type == "tool_use":
            tool_name = part.get("tool") or "tool"
            state = part.get("state") or {}
            status = state.get("status") or "used"
            detail = summarize_tool_input(tool_name, state)
            if detail:
                emit_line("[tool] ", f"{tool_name} {status}: {detail}")
            else:
                emit_line("[tool] ", f"{tool_name} {status}")
            continue

        if event_type != "text":
            continue

        text = part.get("text")
        if isinstance(text, str) and text:
            emit_text_block(text)
' "$log_file" "$SHOW_SESSION"
}

run_once() {
    local run_number="$1"
    local timestamp
    local run_prefix
    local log_file
    local parsed_file
    local summary
    local session_id
    local message
    local -a cmd

    timestamp="$(date -u +%Y%m%d-%H%M%S)"
    run_prefix="$timestamp-$(printf '%04d' "$run_number")"
    log_file="$STATE_DIR/$run_prefix.jsonl"
    parsed_file="$STATE_DIR/$run_prefix.parsed.json"
    message="$(<"$PROMPT_FILE")"

    cmd=(opencode run --format json --dir "$PROJECT_DIR")
    if [[ -n "$MODEL" ]]; then
        cmd+=(--model "$MODEL")
    fi
    if [[ -n "$AGENT" ]]; then
        cmd+=(--agent "$AGENT")
    fi
    if [[ -s "$SESSION_FILE" ]]; then
        session_id="$(tr -d '\n' < "$SESSION_FILE")"
        if [[ -n "$session_id" ]]; then
            cmd+=(--session "$session_id")
        fi
    fi
    cmd+=("$message")

    if [[ "$SHOW_SESSION" -eq 1 ]]; then
        "${cmd[@]}" | capture_run_output "$log_file"
        printf '\n' >&2
    else
        "${cmd[@]}" > "$log_file"
    fi

    extract_summary "$log_file" "$parsed_file"

    session_id="$(json_field "$parsed_file" session_id)"
    summary="$(json_field "$parsed_file" summary)"

    if [[ -n "$session_id" ]]; then
        printf '%s\n' "$session_id" > "$SESSION_FILE"
    fi

    printf '%s\n' "$summary" > "$LATEST_SUMMARY_FILE"
    cp "$log_file" "$LATEST_LOG_FILE"
    cp "$parsed_file" "$LATEST_PARSED_FILE"

    printf '%s\n' "$summary"
}

main() {
    local run_number=1
    local loop_continue

    parse_args "$@"
    require_command opencode
    require_command python3

    if [[ ! -r "$PROMPT_FILE" ]]; then
        echo "Prompt file is not readable: $PROMPT_FILE" >&2
        exit 1
    fi

    mkdir -p "$STATE_DIR"

    if [[ "$FRESH" -eq 1 ]]; then
        rm -f "$SESSION_FILE"
    fi

    while :; do
        run_once "$run_number"
        loop_continue="$(json_field "$LATEST_PARSED_FILE" loop_continue)"

        if [[ "$loop_continue" == "no" ]]; then
            break
        fi

        if [[ "$ITERATIONS" -gt 0 && "$run_number" -ge "$ITERATIONS" ]]; then
            break
        fi

        run_number=$((run_number + 1))

        if [[ "$DELAY_SECONDS" -gt 0 ]]; then
            sleep "$DELAY_SECONDS"
        fi
    done
}

main "$@"
