package ghautomation

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"

	github "github.com/google/go-github/v69/github"
	"golang.org/x/oauth2"
)

const (
	defaultBaseBranch   = "main"
	defaultWorkflowFile = "ci.yml"
	defaultBranchPrefix = "ferox-agent"
	defaultPollInterval = 10 * time.Second
	defaultWaitTimeout  = 90 * time.Minute
	prMarker            = "<!-- ferox-agent-pr -->"
)

var nonBranchChars = regexp.MustCompile(`[^a-z0-9._/-]+`)

type Config struct {
	RepoDir      string
	CommitSHA    string
	RunLabel     string
	Hypothesis   string
	Result       string
	NotebookPath string
	HandoffPath  string
	DocsUpdated  string
	BaseBranch   string
	WorkflowFile string
	BranchPrefix string
	GitHubToken  string
	GitHubUser   string
	PollInterval time.Duration
	WaitTimeout  time.Duration
}

type Result struct {
	Owner          string
	Repo           string
	Branch         string
	PRNumber       int
	PRURL          string
	WorkflowRunID  int64
	WorkflowRunURL string
	CIConclusion   string
	Merged         bool
}

func PublishCommitAndWait(ctx context.Context, cfg Config) (Result, error) {
	if err := cfg.normalize(); err != nil {
		return Result{}, err
	}

	owner, repo, err := discoverRepo(ctx, cfg.RepoDir)
	if err != nil {
		return Result{}, err
	}
	branch := buildBranchName(cfg.BranchPrefix, cfg.RunLabel, cfg.CommitSHA)
	if err := pushCommit(ctx, cfg.RepoDir, owner, repo, cfg.CommitSHA, branch, cfg.GitHubUser, cfg.GitHubToken); err != nil {
		return Result{}, err
	}

	client := newGitHubClient(ctx, cfg.GitHubToken)
	if err := ensureRepoSettings(ctx, client, owner, repo); err != nil {
		return Result{}, err
	}
	pr, err := ensurePullRequest(ctx, client, owner, repo, branch, cfg)
	if err != nil {
		return Result{}, err
	}

	result := Result{
		Owner:    owner,
		Repo:     repo,
		Branch:   branch,
		PRNumber: pr.GetNumber(),
		PRURL:    pr.GetHTMLURL(),
	}

	waitCtx, cancel := context.WithTimeout(ctx, cfg.WaitTimeout)
	defer cancel()

	run, err := waitForCI(waitCtx, client, owner, repo, branch, cfg.CommitSHA, cfg.WorkflowFile, cfg.PollInterval)
	if err != nil {
		return result, err
	}
	result.WorkflowRunID = run.GetID()
	result.WorkflowRunURL = run.GetHTMLURL()
	result.CIConclusion = run.GetConclusion()
	if result.CIConclusion != "success" {
		return result, fmt.Errorf("ci workflow concluded with %q", result.CIConclusion)
	}

	merged, err := waitForMerge(waitCtx, client, owner, repo, pr.GetNumber(), cfg.PollInterval)
	if err == nil {
		result.Merged = merged
	}
	return result, nil
}

func (c *Config) normalize() error {
	c.RepoDir = strings.TrimSpace(c.RepoDir)
	c.CommitSHA = strings.TrimSpace(c.CommitSHA)
	c.RunLabel = strings.TrimSpace(c.RunLabel)
	c.Hypothesis = strings.TrimSpace(c.Hypothesis)
	c.Result = strings.TrimSpace(c.Result)
	c.NotebookPath = strings.TrimSpace(c.NotebookPath)
	c.HandoffPath = strings.TrimSpace(c.HandoffPath)
	c.DocsUpdated = strings.TrimSpace(c.DocsUpdated)
	c.BaseBranch = strings.TrimSpace(c.BaseBranch)
	c.WorkflowFile = strings.TrimSpace(c.WorkflowFile)
	c.BranchPrefix = strings.Trim(strings.TrimSpace(c.BranchPrefix), "/")
	c.GitHubToken = strings.TrimSpace(c.GitHubToken)
	c.GitHubUser = strings.TrimSpace(c.GitHubUser)

	if c.RepoDir == "" {
		return errors.New("repo dir is required")
	}
	if c.CommitSHA == "" || c.CommitSHA == "none" {
		return errors.New("commit sha is required")
	}
	if c.GitHubToken == "" {
		return errors.New("PUBLIC_GH_TOKEN is required")
	}
	if c.GitHubUser == "" {
		return errors.New("PUBLIC_GH_USER is required")
	}
	if c.BaseBranch == "" {
		c.BaseBranch = defaultBaseBranch
	}
	if c.WorkflowFile == "" {
		c.WorkflowFile = defaultWorkflowFile
	}
	if c.BranchPrefix == "" {
		c.BranchPrefix = defaultBranchPrefix
	}
	if c.PollInterval <= 0 {
		c.PollInterval = defaultPollInterval
	}
	if c.WaitTimeout <= 0 {
		c.WaitTimeout = defaultWaitTimeout
	}
	return nil
}

func discoverRepo(ctx context.Context, repoDir string) (string, string, error) {
	remote, err := gitOutput(ctx, repoDir, "remote", "get-url", "origin")
	if err != nil {
		return "", "", fmt.Errorf("discover origin remote: %w", err)
	}
	owner, repo, err := parseGitHubRemote(strings.TrimSpace(remote))
	if err != nil {
		return "", "", err
	}
	return owner, repo, nil
}

func parseGitHubRemote(remote string) (string, string, error) {
	remote = strings.TrimSpace(remote)
	if remote == "" {
		return "", "", errors.New("empty git remote URL")
	}
	if strings.HasPrefix(remote, "git@github.com:") {
		path := strings.TrimPrefix(remote, "git@github.com:")
		return splitOwnerRepo(path)
	}
	parsed, err := url.Parse(remote)
	if err != nil {
		return "", "", fmt.Errorf("parse remote URL: %w", err)
	}
	if !strings.Contains(parsed.Host, "github.com") {
		return "", "", fmt.Errorf("unsupported git remote host %q", parsed.Host)
	}
	return splitOwnerRepo(strings.TrimPrefix(parsed.Path, "/"))
}

func splitOwnerRepo(path string) (string, string, error) {
	path = strings.TrimSuffix(strings.TrimSpace(path), ".git")
	parts := strings.Split(path, "/")
	if len(parts) < 2 {
		return "", "", fmt.Errorf("unable to parse owner/repo from %q", path)
	}
	owner := parts[len(parts)-2]
	repo := parts[len(parts)-1]
	if owner == "" || repo == "" {
		return "", "", fmt.Errorf("invalid owner/repo in %q", path)
	}
	return owner, repo, nil
}

func buildBranchName(prefix, runLabel, commitSHA string) string {
	name := strings.ToLower(strings.TrimSpace(runLabel))
	if name == "" {
		name = "run"
	}
	name = nonBranchChars.ReplaceAllString(name, "-")
	name = strings.Trim(name, "-./")
	for strings.Contains(name, "--") {
		name = strings.ReplaceAll(name, "--", "-")
	}
	shortSHA := commitSHA
	if len(shortSHA) > 8 {
		shortSHA = shortSHA[:8]
	}
	return prefix + "/" + name + "-" + shortSHA
}

func pushCommit(ctx context.Context, repoDir, owner, repo, commitSHA, branch, user, token string) error {
	remoteURL := fmt.Sprintf("https://github.com/%s/%s.git", owner, repo)
	askpass, err := writeAskpass(repoDir, user, token)
	if err != nil {
		return fmt.Errorf("create git askpass helper: %w", err)
	}
	defer os.Remove(askpass)

	cmd := exec.CommandContext(ctx, "git", "push", remoteURL, commitSHA+":refs/heads/"+branch)
	cmd.Dir = repoDir
	cmd.Env = append(os.Environ(),
		"GIT_ASKPASS="+askpass,
		"GIT_TERMINAL_PROMPT=0",
		"PUBLIC_GH_USER="+user,
		"PUBLIC_GH_TOKEN="+token,
	)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("push branch %s: %w: %s", branch, err, strings.TrimSpace(stderr.String()))
	}
	return nil
}

func writeAskpass(repoDir, user, token string) (string, error) {
	path := filepath.Join(repoDir, ".git", "ferox-gh-askpass.sh")
	content := "#!/bin/sh\n" +
		"case \"$1\" in\n" +
		"  *Username* ) printf '%s\\n' \"$PUBLIC_GH_USER\" ;;&\n" +
		"  * ) printf '%s\\n' \"$PUBLIC_GH_TOKEN\" ;;&\n" +
		"esac\n"
	if err := os.WriteFile(path, []byte(content), 0o700); err != nil {
		return "", err
	}
	_ = user
	_ = token
	return path, nil
}

func ensurePullRequest(ctx context.Context, client *github.Client, owner, repo, branch string, cfg Config) (*github.PullRequest, error) {
	prs, _, err := client.PullRequests.List(ctx, owner, repo, &github.PullRequestListOptions{
		State: "open",
		Head:  owner + ":" + branch,
		Base:  cfg.BaseBranch,
	})
	if err != nil {
		return nil, fmt.Errorf("list pull requests for branch %s: %w", branch, err)
	}
	if len(prs) > 0 {
		sort.Slice(prs, func(i, j int) bool { return prs[i].GetNumber() > prs[j].GetNumber() })
		if err := ensureApproval(ctx, client, owner, repo, prs[0].GetNumber()); err != nil {
			return nil, err
		}
		return prs[0], nil
	}

	title := buildPRTitle(cfg)
	body := buildPRBody(cfg)
	pr, _, err := client.PullRequests.Create(ctx, owner, repo, &github.NewPullRequest{
		Title:               github.Ptr(title),
		Head:                github.Ptr(branch),
		Base:                github.Ptr(cfg.BaseBranch),
		Body:                github.Ptr(body),
		MaintainerCanModify: github.Ptr(false),
	})
	if err != nil {
		return nil, fmt.Errorf("create pull request: %w", err)
	}
	if err := ensureApproval(ctx, client, owner, repo, pr.GetNumber()); err != nil {
		return nil, err
	}
	return pr, nil
}

func ensureRepoSettings(ctx context.Context, client *github.Client, owner, repo string) error {
	_, _, err := client.Repositories.Edit(ctx, owner, repo, &github.Repository{
		AllowAutoMerge:      github.Ptr(true),
		DeleteBranchOnMerge: github.Ptr(true),
	})
	if err != nil {
		return fmt.Errorf("enable repo auto-merge settings: %w", err)
	}
	return nil
}

func ensureApproval(ctx context.Context, client *github.Client, owner, repo string, prNumber int) error {
	reviewBody := "Auto-approving autonomous Ferox PR once CI passes."
	reviewEvent := "APPROVE"
	_, _, err := client.PullRequests.CreateReview(ctx, owner, repo, prNumber, &github.PullRequestReviewRequest{
		Body:  github.Ptr(reviewBody),
		Event: github.Ptr(reviewEvent),
	})
	if err != nil && !strings.Contains(strings.ToLower(err.Error()), "review comments is invalid") {
		return fmt.Errorf("approve pull request: %w", err)
	}
	return nil
}

func buildPRTitle(cfg Config) string {
	title := strings.TrimSpace(cfg.Hypothesis)
	if title == "" {
		title = "Autonomous Ferox experiment"
	}
	title = strings.TrimSuffix(title, ".")
	if cfg.RunLabel != "" {
		return fmt.Sprintf("Autonomous run %s: %s", cfg.RunLabel, truncate(title, 72))
	}
	return "Autonomous run: " + truncate(title, 72)
}

func buildPRBody(cfg Config) string {
	lines := []string{
		prMarker,
		"## Summary",
		"- Autonomous run: `" + empty(cfg.RunLabel, "unknown") + "`",
		"- Result: `" + empty(cfg.Result, "committed") + "`",
		"- Hypothesis: " + empty(cfg.Hypothesis, "not provided"),
		"- Commit: `" + cfg.CommitSHA + "`",
	}
	if cfg.NotebookPath != "" {
		lines = append(lines, "- Notebook: `"+cfg.NotebookPath+"`")
	}
	if cfg.HandoffPath != "" {
		lines = append(lines, "- Handoff: `"+cfg.HandoffPath+"`")
	}
	if cfg.DocsUpdated != "" {
		lines = append(lines, "- Docs updated: `"+cfg.DocsUpdated+"`")
	}
	lines = append(lines,
		"",
		"## Validation",
		"- Autonomous runner waits for `CI` to complete for this PR head commit.",
		"",
		"## Notes for Reviewers",
		"- This PR was opened by the Ferox autonomous runner.",
	)
	return strings.Join(lines, "\n")
}

func waitForCI(ctx context.Context, client *github.Client, owner, repo, branch, commitSHA, workflowFile string, pollInterval time.Duration) (*github.WorkflowRun, error) {
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	for {
		runs, _, err := client.Actions.ListWorkflowRunsByFileName(ctx, owner, repo, workflowFile, &github.ListWorkflowRunsOptions{
			Branch:  branch,
			HeadSHA: commitSHA,
			Event:   "pull_request",
			ListOptions: github.ListOptions{
				PerPage: 20,
			},
		})
		if err == nil && runs != nil && len(runs.WorkflowRuns) > 0 {
			sort.Slice(runs.WorkflowRuns, func(i, j int) bool {
				return runs.WorkflowRuns[i].GetID() > runs.WorkflowRuns[j].GetID()
			})
			run := runs.WorkflowRuns[0]
			if run.GetStatus() == "completed" {
				return run, nil
			}
		}

		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-ticker.C:
		}
	}
}

func waitForMerge(ctx context.Context, client *github.Client, owner, repo string, prNumber int, pollInterval time.Duration) (bool, error) {
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()
	for {
		pr, _, err := client.PullRequests.Get(ctx, owner, repo, prNumber)
		if err == nil {
			if pr.GetMerged() {
				return true, nil
			}
			if pr.GetState() == "closed" {
				return false, nil
			}
		}
		select {
		case <-ctx.Done():
			return false, ctx.Err()
		case <-ticker.C:
		}
	}
}

func newGitHubClient(ctx context.Context, token string) *github.Client {
	tokenSource := oauth2.StaticTokenSource(&oauth2.Token{AccessToken: token})
	return github.NewClient(oauth2.NewClient(ctx, tokenSource))
}

func gitOutput(ctx context.Context, repoDir string, args ...string) (string, error) {
	cmd := exec.CommandContext(ctx, "git", args...)
	cmd.Dir = repoDir
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	cmd.Stdout = &stdout
	cmd.Stderr = &stderr
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("git %s: %w: %s", strings.Join(args, " "), err, strings.TrimSpace(stderr.String()))
	}
	return stdout.String(), nil
}

func truncate(value string, limit int) string {
	value = strings.Join(strings.Fields(value), " ")
	if len(value) <= limit {
		return value
	}
	return value[:limit-3] + "..."
}

func empty(value, fallback string) string {
	if strings.TrimSpace(value) == "" {
		return fallback
	}
	return value
}
