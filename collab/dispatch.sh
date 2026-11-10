#!/bin/sh
# usage: collab/dispatch.sh <task-id>   (worktree ../click-<id> on branch grok/<id> must exist)
set -eu
ID="$1"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
exec grok --cwd "$ROOT/../click-$ID" --permission-mode acceptEdits \
  --allow "Bash(git status*)" --allow "Bash(git diff*)" --allow "Bash(git log*)" \
  --allow "Bash(git add*)" --allow "Bash(git commit*)" --allow "Bash(git branch*)" \
  --allow "Bash(ls*)" --allow "Bash(pwd*)" --allow "Bash(grep*)" --allow "Bash(cat*)" \
  --allow "Bash(bin/build*)" --allow "Bash(bin/test*)" --allow "Bash(./bin/*)" --allow "Bash(playerctl*)" \
  --rules "$(cat "$ROOT/collab/GROK.md")" --max-turns 80 \
  -p "Do task collab/tasks/$ID.md. Task id is $ID; write your report to collab/reports/$ID.md."
