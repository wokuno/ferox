#!/bin/bash
# Self-hosted runner setup script for Ferox CI
# Usage: ./scripts/setup-runner.sh

set -e

# Check if GitHub token is provided
if [ -z "$GH_TOKEN" ]; then
    echo "Error: GH_TOKEN environment variable not set"
    echo "Please set your GitHub Personal Access Token:"
    echo "  export GH_TOKEN=your_token_here"
    echo ""
    echo "To create a token:"
    echo "  1. Go to GitHub Settings > Developer settings > Personal access tokens"
    echo "  2. Create new token with 'repo' and 'workflow' scopes"
    exit 1
fi

REPO="wokuno/ferox"
RUNNER_DIR="$HOME/actions-runner"

echo "=== Ferox Self-Hosted Runner Setup ==="
echo ""

# Check for existing runner
if [ -d "$RUNNER_DIR" ]; then
    echo "Warning: Runner already exists at $RUNNER_DIR"
    read -p "Remove and reinstall? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cd "$RUNNER_DIR"
        ./svc.sh stop 2>/dev/null || true
        ./svc.sh uninstall 2>/dev/null || true
        cd ~
        rm -rf "$RUNNER_DIR"
    else
        echo "Using existing runner"
        exit 0
    fi
fi

# Create runner directory
mkdir -p "$RUNNER_DIR"
cd "$RUNNER_DIR"

echo "Downloading GitHub Actions runner..."
ARCH=$(uname -m)
if [ "$ARCH" = "x86_64" ]; then
    ARCH="x64"
elif [ "$ARCH" = "aarch64" ]; then
    ARCH="arm64"
fi

RUNNER_VERSION="2.322.0"
curl -Ls "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-${ARCH}-${RUNNER_VERSION}.tar.gz" | tar xz

echo ""
echo "Configuring runner for repository: $REPO"
echo ""

# Configure runner
./config.sh --url "https://github.com/$REPO" --token "$GH_TOKEN" --labels "self-hosted" --name "ferox-runner" --unattended

echo ""
echo "Installing and starting service..."
./svc.sh install
./svc.sh start

echo ""
echo "=== Setup Complete ==="
echo "Runner should now appear in GitHub under repository settings > Actions > Runners"
echo ""
echo "To check status: $RUNNER_DIR/svc.sh status"
echo "To stop: $RUNNER_DIR/svc.sh stop"
echo "To start: $RUNNER_DIR/svc.sh start"
