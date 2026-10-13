# Progress Log — Beta Branch

Workflow (per user instruction):
- For each change, write every step taken + reason in this file.
- Commit and push to git after each portion is completed.
- Current branch: `Beta` (from `main` @ d4495920).
- Remote: `origin` (SSH: git@github.com:Lyky35/Heretic2Remastered.git).

---

## 2026-10-13 — Create `Beta` branch

Steps:
1. Inspected local repo at `/home/cyklon/Work/Heretic2Remastered` — confirmed git repo with remote `https://github.com/Lyky35/Heretic2Remastered.git`, branch `main`, clean except untracked `fxstaff_debug.log`.
   - Reason: verify starting state before branching.
2. Ran `git fetch origin` — local `main` was stale (ahead by 4); after fetch, `main` == `origin/main` @ `d4495920`.
   - Reason: ensure `Beta` is cut from latest remote `main`, not a stale local.
3. Checked GitHub via `gh repo view` + `git ls-remote --heads origin` — only `main` existed on remote.
   - Reason: confirm `Beta` name is free and avoid collision.
4. Ran `git checkout -b Beta` (local).
   - Reason: create the requested branch from current `main`.
5. Direct `git push -u origin Beta` over HTTPS failed (`could not read Username`) — no credential helper, while `gh` is configured for SSH protocol.
   - Reason: documented failure to explain next step.
6. Created remote branch via `gh api repos/Lyky35/Heretic2Remastered/git/refs --method POST -f ref=refs/heads/Beta -f sha=d4495920...` then `git fetch origin` + `git branch --set-upstream-to=origin/Beta Beta`.
   - Reason: `gh api` uses stored token, bypasses broken HTTPS credential setup; then link local tracking branch.

Result: `Beta` exists locally + remotely, both @ `d4495920`, tracking configured. Verified with `git branch -vv` and `git ls-remote --heads origin`.

## 2026-10-13 — Adopt PROGRESS.md workflow + fix push auth

Steps:
1. Confirmed `PROGRESS.md` did not exist (glob search).
   - Reason: need to create it to satisfy new workflow instruction.
2. Checked SSH: `ssh -T git@github.com` succeeded as `Lyky35`; `~/.ssh/` has `id_ed25519_github`.
   - Reason: HTTPS push failed earlier; SSH is working, so switching remote to SSH fixes future `commit + push after each portion`.
3. Ran `git remote set-url origin git@github.com:Lyky35/Heretic2Remastered.git` + `git fetch origin` (OK).
   - Reason: align remote URL with working SSH auth (matches `gh` config `Git operations protocol: ssh`); preserves same repo, only changes transport.
4. Created this `PROGRESS.md` file.
   - Reason: log every step + reason per instruction, starting now.
5. Next: `git add PROGRESS.md`, commit, `git push origin Beta`.
   - Reason: complete the "commit and push after each portion" portion for this workflow-setup change.
