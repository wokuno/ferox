# Self-Hosted Runner Operations

This document describes how to operate the `ferox` self-hosted GitHub Actions runner safely.

## Runner Profile

- Hostname: `ferox`
- Labels: `self-hosted`, `Linux`, `X64`, `ferox`, `rocky10`
- Runner path: `/home/plover/actions-runner-ferox`
- Service user: `plover`

## Safety Model

- The workflow runs self-hosted jobs only on trusted events (`push` and `workflow_dispatch`).
- Pull request workflows do not run on self-hosted infrastructure.
- Repository Actions policy is set to `selected` with SHA pinning required.
- Allowed third-party action is pinned to:
  - `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683`

## Day-to-Day Checks

### 1) Check runner process on host

```bash
ssh plover@23.150.68.200 "pgrep -af Runner.Listener || true"
```

### 2) Check runner status from GitHub

```bash
gh api repos/wokuno/ferox/actions/runners --jq '.runners[] | {name,status,busy,labels:[.labels[].name]}'
```

### 3) Trigger and watch CI manually

```bash
gh workflow run CI --ref main
gh run watch --exit-status
```

## Token Rotation / Re-Registration

Re-register the runner when rotating credentials or rebuilding the host.

```bash
TOKEN=$(gh api repos/wokuno/ferox/actions/runners/registration-token -X POST --jq .token)
ssh plover@23.150.68.200 "cd /home/plover/actions-runner-ferox && ./config.sh --unattended --url https://github.com/wokuno/ferox --token $TOKEN --name ferox --labels ferox --work _work --replace"
```

After rotation, verify runner status in GitHub and run a manual workflow dispatch.

## Restart Procedure

If jobs are stuck in queued state:

```bash
ssh plover@23.150.68.200 "pkill -f Runner.Listener || true; cd /home/plover/actions-runner-ferox && nohup ./run.sh > runner.log 2>&1 < /dev/null &"
```

Then verify registration and dispatch a test workflow.

## Incident Response

If untrusted code is suspected to have executed on the runner:

1. Disable runner in repository settings or stop process immediately.
2. Revoke and rotate any tokens/credentials present on the host.
3. Rebuild the VM from a clean base image.
4. Re-register runner with a fresh token.
5. Review Actions policy, branch protection, and workflow trigger filters before re-enabling.

## Change Control Checklist

When modifying CI workflows:

- Keep third-party actions pinned by full commit SHA.
- Do not enable self-hosted jobs for pull request events.
- Avoid secrets exposure in logs.
- Validate on `workflow_dispatch` before relying on `main` push execution.
