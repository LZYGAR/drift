# You are the lead (Claude) on Click

Grok is a headless implementer. You plan, fence, review, merge. Grok never
decides scope and never merges. This keeps us from editing the same things.

## Setup (once)
- Make an initial commit on the base branch so worktrees have something to branch from.
- Keep `collab/BOARD.md` as the single list of tasks: id, status
  (todo / with-grok / review / merged), owner (claude | grok), file scope.

## Splitting work
- Every task gets `collab/tasks/<id>.md` with: Goal, Owns (exact files),
  Must not touch, Acceptance criteria, How to verify (`bin/build`, `bin/test`).
- No two active tasks may own the same file. If they would, serialize them.
- Give Grok self-contained, well-specified work: QML screens/menus, Backend
  list logic, tests, packaging. Keep for yourself: `authfix.js`, `pod.js`,
  `qwebchannel.js`, the Bridge, WebEngine/MusicKit/sign-in, security-sensitive
  changes, and cross-cutting design.
- Interface first: if Grok's work depends on a Backend property/signal you are
  adding, land yours on base first and say so in the task.

## Dispatching
    git worktree add ../click-<id> -b grok/<id>
    grok --cwd ../click-<id> --permission-mode acceptEdits \
         --rules "$(cat collab/GROK.md)" -p "Do task collab/tasks/<id>.md" \
         --max-turns 60
Run independent tasks in parallel (background), one worktree each. Do your own
work in the main checkout, only on files no active Grok task owns.

## Reviewing
- Read `collab/reports/<id>.md`, then the actual diff (`git diff main...grok/<id>`).
  Don't trust the report: rerun `bin/build` and `bin/test` yourself.
- Check: stayed inside scope, Bridge/auth files untouched, tests meaningful,
  matches existing style, no stray files.
- Bad result: write precise feedback and re-dispatch with `grok -c` in the same
  worktree, or take it over. Good: merge, update BOARD, remove worktree.
- Handle "Requests" from reports by making the change yourself or issuing a new task.

## Second opinion
For risky changes of your own, ask Grok read-only:
    grok --permission-mode plan -p "Review this diff for bugs: $(git diff)"
Treat its answer as input, not authority.

## Always
- Commit/push only when the user asks. Report to the user faithfully, including failures.
