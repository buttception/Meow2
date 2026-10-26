# Working with AI agents (Claude Code)

Meow2 is developed with AI agents, mostly Claude Code running in the desktop app on the owner's Windows PC. The rules agents follow are in [AGENTS.md](../../AGENTS.md); this page explains how the setup works.

## How Claude Code finds the rules

Claude Code automatically loads `CLAUDE.md` from the repository root at the start of every session. Meow2's `CLAUDE.md` contains almost nothing itself. It imports the real rule files with `@` lines:

```markdown
@AGENTS.md
@techstack.md
@milestones.md
```

So there is a single source of truth (`AGENTS.md` is also what other agent tools read), and Claude Code still sees all of it without being told.

## One worktree per session

A [git worktree](https://git-scm.com/docs/git-worktree) is a second (third, …) working folder attached to the same repository. Each one has a different branch checked out, and they share the object database, so creating one is fast and cheap.

The desktop app creates one worktree per session under `F:\Meow2\.claude\worktrees\<name>\`. This lets several features be open at once without stashing or switching branches. The `.claude/worktrees/` folder is listed in `.git/info/exclude`, so:
- `git status` in the main folder doesn't show the worktrees;
- the text-file lint (`git ls-files`) and the format target (globs over the project folders only) never pick up files from them.

At the start of feature work the agent:

```
git fetch origin
git switch -c feat/<issue#>-<name> origin/main
git submodule update --init --recursive
```

Things to know:
- **Submodules are per worktree.** A new worktree has empty `external/` folders until `git submodule update --init --recursive` runs.
- **Build folders are per worktree** (`build/<preset>/` inside each one), so the first build in a new worktree is a full build.
- **A branch can be checked out in only one worktree at a time.** To continue a branch that's open elsewhere, use that worktree.
- Parallel worktrees aren't parallel PRs on the same files. Features that touch the same files (top-level `CMakeLists.txt`, `milestones.md`, `docs/wiki/Home.md` are the usual ones) still wait for each other or are stacked explicitly.
- After a PR is merged, the session can be archived and its worktree removed (`git worktree remove <path>`, or the app's clean-up).

## Building from an agent shell

MSVC needs the Visual Studio environment (`vcvars64.bat`). The owner's PowerShell execution policy is `AllSigned`, which refuses unsigned `.ps1` scripts. Agents therefore run builds through `cmd`:

```
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && cmake --workflow --preset windows-msvc-debug"
```

The Windows CI job on the self-hosted runner does the same thing (see [CI](CI.md)).

## What runs where

| Check | Where |
|---|---|
| Windows presets (MSVC Debug / RelWithDebInfo / Release) | locally, and Windows CI |
| Linux presets (Clang), GCC | Linux CI only (no local Linux toolchain) |
| GPU / visual tests (later milestones) | locally once they exist |

Agents read CI results with `gh pr checks <pr>` and report them in the PR. A test that didn't run is reported as not run, never as passed.
