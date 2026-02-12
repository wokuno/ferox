# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| main    | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in Ferox, please report it by:

1. **Do NOT** open a public GitHub issue
2. Email the maintainer directly or use GitHub's private vulnerability reporting

We will acknowledge receipt within 48 hours and provide a detailed response within 7 days.

## Security Measures

### GitHub Actions

Our CI/CD pipeline follows security best practices:

- **Minimal permissions**: All jobs run with `contents: read` only
- **Pinned actions**: Actions are pinned to specific commit SHAs, not tags
- **No credential persistence**: `persist-credentials: false` on all checkouts
- **Branch protection**: Only `main` and `develop` branches trigger CI on push
- **PR restrictions**: PRs only run CI when targeting `main`

### Code Review

- All changes require review from code owners (see `CODEOWNERS`)
- Workflow file changes require explicit owner approval
- Direct pushes to `main` should be restricted (configure in repo settings)

### Recommended Repository Settings

For maximum security, configure these settings in your GitHub repository:

1. **Branch Protection Rules** (Settings → Branches → Add rule for `main`):
   - [x] Require a pull request before merging
   - [x] Require approvals (1+)
   - [x] Dismiss stale pull request approvals when new commits are pushed
   - [x] Require review from Code Owners
   - [x] Require status checks to pass before merging
   - [x] Require branches to be up to date before merging
   - [x] Do not allow bypassing the above settings

2. **Actions Permissions** (Settings → Actions → General):
   - [x] Allow select actions and reusable workflows
   - [x] Allow actions created by GitHub
   - [x] Require approval for all outside collaborators

3. **Fork PR Workflows** (Settings → Actions → General):
   - [x] Require approval for first-time contributors

## Dependencies

This project has minimal external dependencies:
- SDL2 (optional, for GUI client)
- Standard C library
- POSIX threads

No package managers or external build dependencies that could be compromised.
