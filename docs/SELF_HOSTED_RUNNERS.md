# Self-Hosted Runners for Ferox CI

This document describes how to set up and use self-hosted runners for Ferox CI.

## Why Self-Hosted Runners?

- Faster builds (no queue time on GitHub runners)
- More control over environment
- Can run on your own hardware/VMs

## Setting Up a Runner

### Prerequisites

1. A machine (VM or physical) with:
   - Ubuntu 24.04 or similar Linux distribution
   - At least 2 CPU cores, 2GB RAM
   - GitHub Personal Access Token (PAT) with `repo` and `workflow` scopes

2. Install dependencies:
```bash
sudo apt-get update
sudo apt-get install -y cmake build-essential libsdl2-dev curl
```

### Quick Setup

1. Set your GitHub token:
```bash
export GH_TOKEN=your_github_token_here
```

2. Run the setup script:
```bash
./scripts/setup-runner.sh
```

### Manual Setup

If you prefer to set up manually:

```bash
# Create runner directory
mkdir -p ~/actions-runner
cd ~/actions-runner

# Download runner
curl -Ls https://github.com/actions/runner/releases/download/v2.322.0/actions-runner-linux-x64-2.322.0.tar.gz | tar xz

# Configure (get token from: Settings > Actions > Runners > New self-hosted runner)
./config.sh --url https://github.com/wokuno/ferox --token YOUR_TOKEN --labels self-hosted

# Install as service
./svc.sh install
./svc.sh start
```

## Using Self-Hosted Runners

### Trigger a PR build on self-hosted runner

In your PR comment, use:
```
/selfhosted
```

Or manually trigger from GitHub Actions workflow dispatch.

### Runner Labels

- `self-hosted` - All self-hosted runners

## Killdeer VM Setup

For using killdeer.plover.digital VMs as runners:

1. Create a VM with Ubuntu 24.04:
```bash
ssh killdeer.plover.digital create ferox-runner Basic ubuntu-24.04
```

2. Wait for it to boot and note the IP address

3. The VM needs to be accessible via SSH with your key

4. Install the runner on the VM using the steps above

## Troubleshooting

### Runner not appearing

- Check service status: `~/actions-runner/svc.sh status`
- Check logs: `~/actions-runner/_diag/`

### Runner not picking up jobs

- Verify labels match: check GitHub > Settings > Actions > Runners
- Check repository settings allow self-hosted runners

### Network issues

- Ensure the runner can reach: github.com, api.github.com
- Check firewall rules on VM
