#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
# SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

# Brings the next pull request set to auto-merge that is behind main up to date, unless one of
# them that is up to date still runs its CI (docs/17, section 8). It reads with GH_TOKEN and
# updates with UPDATE_BRANCHES_TOKEN, whose pushes start CI where the workflow's own token's
# pushes do not.
set -euo pipefail

# The workflow whose checks main requires.
readonly ciWorkflow="CI"
# How long the update's CI may take to start: this many polls, this many seconds apart.
readonly startPolls=60
readonly pollSeconds=5

if [ -z "${UPDATE_BRANCHES_TOKEN:-}" ]; then
  echo "::error::The UPDATE_BRANCHES_TOKEN secret is missing (docs/17, section 8)." >&2
  exit 1
fi

ciChecks="[.statusCheckRollup[] | select(.workflowName == \"$ciWorkflow\")]"
ciRunning="any(${ciChecks}[]; .status != \"COMPLETED\")"
ciFailed="any(${ciChecks}[]; .status == \"COMPLETED\" and .conclusion != \"SUCCESS\" and .conclusion != \"SKIPPED\")"

# The open pull requests into main set to auto-merge, a line each, "<number> <head commit>
# <whether its CI is running>": oldest first, but those whose CI last failed after the others,
# so that a broken one does not hold the rest back each time main moves.
queued="$(gh pr list --base main --state open --limit 100 \
  --json number,isDraft,autoMergeRequest,headRefOid,statusCheckRollup \
  --jq "map(select(.autoMergeRequest != null and (.isDraft | not))) | sort_by([$ciFailed, .number])[]
        | \"\\(.number) \\(.headRefOid) \\($ciRunning)\"")"

behindBy() { gh api "repos/{owner}/{repo}/compare/main...$1" --jq .behind_by; }

# One up to date with main whose CI is running merges or fails first: updating another now would
# only start CI that its merge makes stale. One behind main cannot merge, so it holds nothing.
while read -r number head ciRuns; do
  [ -n "$number" ] && [ "$ciRuns" = true ] || continue
  if [ "$(behindBy "$head")" -eq 0 ]; then
    echo "#$number is up to date with main and its CI is running: it goes first."
    exit 0
  fi
done <<<"$queued"

while read -r number head _; do
  [ -n "$number" ] || continue
  behind="$(behindBy "$head")"
  [ "$behind" -gt 0 ] || continue

  if ! output="$(GH_TOKEN="$UPDATE_BRANCHES_TOKEN" gh api --method PUT \
    "repos/{owner}/{repo}/pulls/$number/update-branch" -f expected_head_sha="$head" 2>&1)"; then
    # 422 is a conflict with main, which the pull request's author resolves, or a push since the
    # list was read; any other failure is this script's.
    if [[ "$output" == *"HTTP 422"* ]]; then
      echo "::warning::#$number is behind main but was not updated: $output"
      continue
    fi
    echo "$output" >&2
    exit 1
  fi
  echo "#$number was $behind commits behind main: updated."

  # GitHub updates the branch asynchronously. Once its CI has started, the next run of this
  # script sees it running instead of updating a second pull request.
  for _ in $(seq "$startPolls"); do
    started="$(gh pr view "$number" --json headRefOid,statusCheckRollup \
      --jq ".headRefOid != \"$head\" and any(.statusCheckRollup[]; .workflowName == \"$ciWorkflow\")")"
    [ "$started" = true ] && exit 0
    sleep "$pollSeconds"
  done
  echo "::error::#$number was updated, but its CI did not start." >&2
  exit 1
done <<<"$queued"

echo "No pull request set to auto-merge is behind main."
