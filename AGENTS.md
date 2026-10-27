# AGENTS.md — Development guidelines for Meow2

**Every agent (main session or subagent) must read this file, [techstack.md](techstack.md) and [milestones.md](milestones.md) in full before doing any work.**
If anything here conflicts with a task prompt, stop and ask the owner.
When this file changes, the agent that changed it must tell the owner what changed (see Changelog).

---

## 1. Scope & permissions

1. Work **only inside the repository folder** (`Meow2/`, including its worktrees under `.claude/worktrees/`). Do not read, search, or write outside it. One exception: the temporary scratchpad folder Claude Code gives each session, for throwaway files only (helper scripts, logs); nothing in it may be needed by a commit.
2. **Ask the owner before installing anything.** This covers system packages, SDKs, pip/npm packages, prebuilt tools (DXC, Agility SDK), **adding/bumping a git submodule**, and initialising the large glTF sample-asset submodules.
3. **Ask the owner before starting any subagent.** Keep the number small. The default is one agent per task, and never one agent per file.
4. **Work only on what the owner asks for.** Milestones in milestones.md are a map, not a to-do list. If the requested feature's order doesn't make sense (it depends on something missing), say so before starting.
5. Never push to `main`, merge PRs, delete branches on the remote, or rewrite `main`'s history. **The owner reviews and merges.**
6. Never commit secrets, build output, or files listed in `.gitignore`.

## 2. Work unit: 1 small feature = 1 issue = 1 branch = 1 PR

### 2.1 Issue (before writing code)
Every feature starts as a GitHub issue using this template:

```
Title: [M<milestone>] <short imperative feature name>

## Goal
<one paragraph: what and why>

## Scope
- in:  ...
- out: ...

## Design
<interfaces / classes / files touched>

## Test cases
| ID | Test name (CTest) | Type (unit/integration/gpu) | Given / When / Then |
|----|-------------------|-----------------------------|---------------------|
| T1 | Log.FileSink.WritesToPath | unit | given a temp path, when INFO is logged and flushed, then the file contains the message |

## Acceptance
- [ ] all test cases above pass via `ctest`
- [ ] no new warnings
- [ ] wiki page added/updated
```

Labels: `milestone:M<n>`, `module:<Module>`, `feature` / `bug` / `chore`.

### 2.2 Branch
- Name: `feat/<issue#>-<kebab-name>` (also `fix/`, `chore/`, `docs/`).
- Always branch from the **latest `origin/main`**: `git fetch origin && git switch -c feat/12-file-sink origin/main`.
- Keep it **small**: one feature, ideally < ~400 changed lines excluding tests.
- **Do not merge `main` into a feature branch.** Use rebase.

### 2.3 Avoiding merge conflicts
- Before opening a PR and after every merge to `main`: `git fetch origin && git rebase origin/main`, then rebuild and re-run the tests.
- Push rebased branches with `git push --force-with-lease` (feature branches only — never `main`).
- By default, have **one PR open at a time**. Open a second one only if it touches entirely different files. If a feature depends on an unmerged PR, wait, or stack it explicitly and say so in the PR.
- Shared "hot" files (top-level `CMakeLists.txt`, `.gitmodules`, `CMakePresets.json`): change only the lines you need, and keep lists sorted alphabetically so parallel additions don't collide.
- Line endings are controlled by `.gitattributes`. Never commit line-ending-only changes.

### 2.4 Tests (must pass before a PR)
- Every test case in the issue is implemented and registered with CTest.
- Unit tests use GoogleTest. Add them with `meow2_add_test_suite(TARGET … MODULE … SOURCES …)` (see `docs/wiki/Testing.md`); `TEST(Unit, Behaviour)` in module `Module` becomes the CTest test `Module.Unit.Behaviour`.
- Every executable gets a smoke test (`meow2_add_smoke_test`).
- Labels: `unit`, `smoke`, `integration`, `gpu`, `visual`, `windows`.
- Required before opening a PR:
  ```
  cmake --preset <preset>
  cmake --build --preset <preset>
  ctest --preset <preset> --output-on-failure
  ```
  **All tests green, including pre-existing ones.**
- Run the final check from a **clean clone** of the branch (`git clone --no-local . <tmp> -b <branch>` + `git submodule update --init`), not just the working folder. Untracked or git-ignored files (e.g. a file matched by a `.gitignore` rule) make local runs pass while CI fails. Also check `git status --ignored` for anything that should have been committed. Run at least `Debug`; run `RelWithDebInfo` too when the change touches optimisation-sensitive code (math, memory, threading).
- Visual (golden-image) tests: never update a golden image to make a test pass. Golden updates go in their own commit, with before/after images in the PR, and need the owner's sign-off.
- **⚠ The owner is still deciding how agents should handle test cases.** Until this section is updated, follow the rules above.
- If a test cannot run in the agent's environment (e.g. the Linux/Clang presets on the Windows PC), say so explicitly in the PR and ask the owner to run them. **Never claim a test passed that was not run.**

### 2.5 Pull request
The PR description must contain:
1. `Closes #<issue>`
2. **What** — the feature in 2–3 sentences.
3. **How it works** — design, key classes/functions, data flow. Written for the owner, who reviews it.
4. **Why it's done this way** — the owner uses PRs to *learn*, so this section teaches:
   - the underlying **concepts** (e.g. what a minidump is, why a frame arena resets per frame, what a descriptor heap is), explained from first principles;
   - **each significant decision** and the reason for it;
   - the **alternatives considered** and why they were rejected (trade-offs: performance, simplicity, portability, future features);
   - pitfalls and gotchas this design avoids;
   - links to good references (official docs/specs) for further reading.
5. **Files changed** — short list with one-line purpose each.
6. **Tests** — the test table and the actual `ctest` output summary (passed/failed/not run + why).
7. **Wiki / docs** — page(s) added/updated in `docs/wiki/`; feature status updated in `milestones.md`.
8. **Follow-ups** — anything deliberately left out.

### 2.6 Code review (by the agent, on its own PR)
After opening the PR, the agent reviews the diff and **posts the review as a PR comment**:

```
## Agent code review
Verdict: SOUND / NEEDS CHANGES

### Checked
- [ ] matches the issue's scope (no extras)
- [ ] naming conventions (techstack.md §16); `Format.Check` passes
- [ ] new targets call `meow2_set_warnings`; zero warnings on GCC and Clang
- [ ] interfaces respected — no module includes another module's implementation headers
- [ ] interface policy (techstack.md §3): build-time selection, virtuals only in the RHI / `IAllocator`
- [ ] dependencies injected via constructor; no new globals/singletons/mutable statics (or `// GLOBAL:` justified)
- [ ] thread safety documented for new classes; no lock held across calls into other code
- [ ] ownership/lifetimes clear, no leaks, RAII
- [ ] thread safety stated where relevant
- [ ] errors handled / logged
- [ ] tests cover the acceptance criteria, incl. failure paths
- [ ] rebased on latest main, no conflicts

### Findings
| Severity | File:line | Issue | Action |
```

Fix NEEDS CHANGES items before handing to the owner. The owner then approves and merges.

### 2.7 Versioning (techstack.md §21)
- **Feature PRs never change `project(Meow2 VERSION …)`.**
- The PR that **closes a milestone** bumps MINOR (`0.<milestone+1>.0`, e.g. closing M1 → `0.2.0`) and says so in its description.
- After that PR is merged, an annotated tag `vX.Y.Z` goes on its merge commit on `main`. Agents create or push tags only with the owner's go-ahead; tags are never moved or deleted.
- PATCH changes only in a `fix/` PR against a `release/0.Y` branch, for a bug in an already-tagged release.

## 3. Code rules

- C++20, follow `techstack.md` §16 naming exactly:
  PascalCase types, camelCase functions, parameters and locals, `m_member`, `s_static`, `c_constOrConstexpr` (precedence `c_` > `s_` > `m_`), `I` prefix only for runtime-virtual interfaces. No leading underscores. Fields of plain data structs (data only, no behaviour, e.g. `Transform`, `<Name>Deps`) are plain camelCase without `m_`.
- `.clang-format` is authoritative. Before committing run `cmake --build --preset <p> --target format`; the `Format.Check` test must pass.
- Every Meow2 target (library, executable, test suite) calls `meow2_set_warnings(<target>)`. Warnings are errors. Never call it on third-party targets, and never silence a warning with a pragma without a comment explaining why.
- Public headers in `include/Meow2/<Module>/`, private code in `src/<Module>/`.
- **Dependency injection (techstack.md §18):** classes get every dependency through their constructor (references; a `<Name>Deps` struct when there are more than ~3). Subsystems are created and owned only by the composition root (each executable's `main`/`Application`).
- **No mutable globals**: no singletons, service locators, mutable globals or mutable `static`s. Any unavoidable exception gets a `// GLOBAL: <why>` comment and is called out in the PR.
- **Threading (techstack.md §19):** every class header states its thread safety (not thread-safe / immutable after construction / thread-safe + how). No locks held while calling into other subsystems or callbacks.
- **Editor (techstack.md §20):** every editor feature is implemented as a command (execute + undo + parameter schema) registered in `EditorCommandRegistry`; UI code only issues commands. A feature is done only when it works through MCP too and has a test for it.
- Every subsystem is reached through its public header. Backends are chosen at build time (pimpl + CMake option); only the RHI uses virtual interfaces and runtime selection.
- Third-party headers (SDL3, spdlog, Jolt, vulkan.h, d3d12.h, Qt) must **not** leak into public engine headers, except DirectXMath in `Meow2/Math`.
- Above the RHI, never cache backend-native handles (needed for the Vulkan ↔ D3D12 hot swap).
- No raw `new`/`delete` in engine code. Use `std::unique_ptr`, engine handles, or an engine allocator (`IAllocator`).
- Engine code must compile with both MSVC and Clang (future Linux/Android).
- Coordinate system: left-handed, +X right, +Y up, +Z forward; reversed infinite-far Z, depth 0..1 (techstack.md §10).
- OS-specific code lives only in `src/<Module>/Win32|Linux|Android/`, guarded by `MEOW2_PLATFORM_*`.
- Code must compile in all three configs: Debug, RelWithDebInfo, Release.
- glTF sample assets are fetched on demand (techstack.md §13), never committed.
- Third-party code under `external/` is never edited. Fix issues via wrapper code or an upstream tag bump.

## 4. Documentation

- **README.md** — keep the "Status", "Building", and "Running tests" sections current whenever a PR changes them.
- **Wiki** — every feature PR adds or updates a page in `docs/wiki/` (Markdown, one page per module, e.g. `docs/wiki/Logging.md`, plus `docs/wiki/Home.md` as the index).
  The pages are written as user/developer docs: what the feature does, how to use it, and how it works.
  `docs/wiki/` is the source of truth. It is published to the GitHub wiki (`Meow2.wiki.git`) when the agent has repo access; otherwise the owner publishes it.
- **techstack.md** — update it when a stack decision changes, and add a Changelog line.
- **milestones.md** — the roadmap. Update a feature's status and issue/PR link in every PR.

## 5. Environments

- **Agent workspace:** Claude Code (desktop app) on the owner's Windows 11 PC, in the repo at `F:\Meow2`. Agents build and run the **Windows (MSVC) presets locally**, push with the owner's git credentials, and use `gh` for issues, PRs and CI results. Details: `docs/wiki/AgentWorkflow.md`.
- **One worktree per session/feature:** each Claude Code session works in its own git worktree under `F:\Meow2\.claude\worktrees\` (git-excluded, so lint and format never see it), started from a freshly fetched `origin/main` (§2.2). Each worktree has its own `build/` and its own submodule checkout (`git submodule update --init --recursive` after creating it). A branch can be checked out in only one worktree at a time. Parallel worktrees don't relax §2.3: features touching the same files still wait or stack.
- **Visual Studio environment:** MSVC builds need `vcvars64.bat`. The owner's PowerShell execution policy is `AllSigned`, so don't write unsigned `.ps1` helpers; run builds through `cmd` (`call vcvars64.bat && cmake --workflow --preset …`).
- **Not available locally:** the Linux presets (Clang/GCC) and sanitizers. The hosted Linux CI job covers them; report its result from the PR checks. GPU and visual tests (later milestones) can run locally once they exist; say in the PR whether they ran.
- **CI:** Linux on GitHub-hosted runners, Windows on the owner's self-hosted runner (see `docs/wiki/CI.md`). Agents read the PR's checks with `gh pr checks` and report them; if the runner is offline, say the Windows checks are pending.
- **CI safety (public repo + runner on the owner's PC):** never let a self-hosted job run code from forks, never use `pull_request_target`, never add secrets to self-hosted jobs, never interpolate untrusted `${{ github.event.* }}` text into `run:` scripts, and pin every third-party action to a full commit SHA. Any change under `.github/` must call this out in the PR.

## 6. Checklist per feature (in order)

1. Read AGENTS.md + techstack.md + milestones.md. Check the requested feature's dependencies exist; if not, tell the owner.
2. Create/claim the issue, with test cases.
3. Branch from the latest `origin/main`.
4. Write tests first (where practical), then code.
5. Build + `ctest` green.
6. Update the wiki page and README if needed.
7. Rebase on `origin/main`, re-test, push.
8. Open the PR with the full description.
9. Post the agent code review as a PR comment.
10. Hand off to the owner and wait. Do not start a conflicting branch.

---

## Changelog
- 2026-10-26 — Initial version: scope, permissions, issue/branch/PR workflow, tests, review, naming, docs/wiki, environments.
- 2026-10-26 — Dependencies as git submodules (adding/bumping needs permission); golden-image approval rule; run tests in Debug (+ RelWithDebInfo for sensitive code); MSVC + Clang; coordinate rule; `external/` read-only; test strategy pending owner decision.
- 2026-10-26 — Removed the `_` prefix for locals (plain camelCase); interface policy = build-time selection except the RHI (runtime hot swap), no caching backend handles above the RHI; work only on owner-requested features and flag ordering problems; glTF assets fetched on demand.
- 2026-10-26 — PRs must include a "Why it's done this way" section (concepts, decisions, alternatives, pitfalls, references); roadmap moved to milestones.md, which every PR keeps up to date.
- 2026-10-26 — GoogleTest chosen: `meow2_add_test_suite` / `meow2_add_smoke_test` rules and labels; Environments rewritten (agents build/test in the cloud workspace, the owner's VM has no network and is only used for syncing).
- 2026-10-26 — Code rules: run the `format` target / `Format.Check`; every Meow2 target uses `meow2_set_warnings`; review checklist covers both.
- 2026-10-26 — CI: agents report CI results (Windows via the self-hosted runner); CI safety rules for workflow changes.
- 2026-10-26 — Code rules: dependency injection + composition root, no mutable globals (`// GLOBAL:` exceptions), thread-safety documentation; review checklist updated.
- 2026-10-26 — Tests must also pass from a clean clone (lesson from a git-ignored file that broke CI); editor features = commands exposed over MCP.
- 2026-10-27 — Moved to Claude Code on the owner's Windows PC: `CLAUDE.md` imports this file; Environments rewritten (local MSVC builds, one worktree per session under `.claude/worktrees/`, `cmd` + `vcvars64` because of `AllSigned`, `gh` for GitHub); §1 allows worktrees and the session scratchpad.
- 2026-10-27 — §3 naming: plain data struct fields drop `m_`. CI also runs RelWithDebInfo; clang-format pinned to 19.
- 2026-10-27 — §2.7 Versioning: only milestone-closing PRs bump the version (MINOR), tag `vX.Y.Z` after merge with the owner's go-ahead, PATCH only for fixes to tagged releases.
