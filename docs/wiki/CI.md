# Continuous integration (GitHub Actions)

Every PR and every push to `main` builds and tests Meow2 automatically:

| Job | Runs on | When |
|---|---|---|
| `Linux (Clang Debug)`, `Linux (Clang Release)`, `Linux (GCC Debug)` | GitHub-hosted Ubuntu 24.04 (free for public repos) | every PR (including forks), push to `main` |
| `Windows (MSVC debug)`, `Windows (MSVC release)` | **your PC** (self-hosted runner, label `meow2`) | push to `main`, PRs from branches **in this repo**, manual runs. **Never for PRs from forks.** |

Workflow: [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml). Each job runs the same thing you run locally: `cmake --workflow --preset …`.

---

## Why a self-hosted runner on a public repo needs care

A runner executes whatever the workflow file and the code being built tell it to. GitHub's own guidance is to *not* use self-hosted runners with public repos, because anyone can open a pull request from a fork, and that PR's code (or a modified workflow) would then run on your PC. Meow2 stays public by stacking several defences, so that no single mistake exposes the PC:

1. **The workflow never sends fork code to your PC.** The Windows job has
   `if: github.event_name != 'pull_request' || github.event.pull_request.head.repo.full_name == github.repository`.
   Only people with write access (you, and Claude through the GitHub App you installed) can push branches to this repo.
2. **Repo settings block untrusted workflow runs** (Step 2 below), as a second line of defence if the `if:` were ever removed.
3. **The runner is a non-admin Windows account** (Step 3). Even trusted-but-buggy code can't touch your files, install software or read your browser profile.
4. **No secrets and a read-only token.** Jobs get `contents: read` only, `persist-credentials: false` keeps the token out of `.git/config` on disk, and no repository secrets exist.
5. **Pinned actions.** `actions/checkout` is referenced by full commit SHA, so a hijacked tag can't swap in different code.
6. **`main` is protected** (Step 1): no direct pushes, no force-pushes, no deletion, merge only through a PR whose checks pass.

**What remains:** anyone with write access can still run code on your PC by pushing a branch. Keep write access to yourself (and the Claude GitHub App). Workflow changes always arrive as PRs you can read; check `.github/workflows/` changes in particular.

---

## Step 1 — Protect `main` (do this now)

GitHub → **Settings → Rules → Rulesets → New ruleset → New branch ruleset**:
- Name: `Protect main`, Enforcement: **Active**
- Target branches: **Add target → Include default branch**
- Tick:
  - ✅ **Restrict deletions**
  - ✅ **Block force pushes**
  - ✅ **Require a pull request before merging** → Required approvals: **0** (you can't approve your own PRs); *Allowed merge methods*: **Merge** only (keeps stacked PRs valid)
- Leave **Bypass list** empty
- **Create**

**After the CI PR is merged** (the checks only exist from then on), add required checks. Easiest: **Rulesets → ⋯ → Import a ruleset** with [`.github/rulesets/protect-main.json`](../../.github/rulesets/protect-main.json) (delete the old one first). Or edit the ruleset → ✅ **Require status checks to pass** → add `Linux (Clang Debug)`, `Linux (Clang Release)`, `Linux (GCC Debug)`, `Windows (MSVC debug)`, `Windows (MSVC release)`.
(Don't add required checks before that: open PRs without the workflow would be unmergeable.)

## Step 2 — Lock down Actions settings

GitHub → **Settings → Actions → General**:
- **Actions permissions:** *Allow buttception, and select non-buttception, actions and reusable workflows* → ✅ *Allow actions created by GitHub* (nothing else). If shown, ✅ *Require actions to be pinned to a full-length commit SHA*.
- **Fork pull request workflows from outside collaborators:** **Require approval for all external contributors**.
- **Workflow permissions:** **Read repository contents and packages permissions**; ❌ *Allow GitHub Actions to create and approve pull requests*.
- **Save** each section.

## Step 3 — Prepare a locked-down Windows account

In an **admin** PowerShell:
```powershell
# 1. A standard (non-admin) local user just for the runner
$pw = Read-Host -AsSecureString "Password for meow2-runner"
New-LocalUser -Name "meow2-runner" -Password $pw -PasswordNeverExpires -UserMayNotChangePassword -Description "GitHub Actions runner for Meow2"
# (do NOT add it to Administrators)

# 2. A folder for the runner, owned by you, writable by the runner account
New-Item -ItemType Directory -Path C:\actions-runner
icacls C:\actions-runner /grant "meow2-runner:(OI)(CI)M"
```

**Check your other drives.** Windows profiles (`C:\Users\<you>`) are private to you, but other drives such as `F:\` often grant *Authenticated Users* write access, which would include `meow2-runner`. Check with `icacls F:\`. If you see `Authenticated Users:(…M…)` or `Users:(…M…)`, deny the runner account on drives with personal data:
```powershell
icacls F:\ /deny "meow2-runner:(OI)(CI)(M)"
```
(The runner never needs `F:\Meow2`: it clones its own copy into `C:\actions-runner\_work`.)

## Step 4 — Install the build tools (system-wide)

- **Visual Studio 2022** (Community, or *Build Tools for Visual Studio 2022*) with the **Desktop development with C++** workload. It includes MSVC, *C++ CMake tools for Windows* (CMake + Ninja). Optional: *C++ Clang tools for Windows* (clang-format for `Format.Check`).
- **Git for Windows** (installed for all users, so `git` is on the system PATH). Without it, checkout can't fetch submodules.

## Step 5 — Register the runner

1. GitHub → **Settings → Actions → Runners → New self-hosted runner → Windows, x64**.
2. In an **admin** PowerShell, `cd C:\actions-runner`, then run GitHub's *Download* commands from that page.
3. Configure, using the token shown on that page (valid for 1 hour):
   ```powershell
   .\config.cmd --url https://github.com/buttception/Meow2 --token <TOKEN> `
       --name "$env:COMPUTERNAME-meow2" --labels meow2 --work _work `
       --runasservice --windowslogonaccount ".\meow2-runner" --windowslogonpassword "<password>"
   ```
   The labels `self-hosted, Windows, X64` are added automatically; `meow2` is what the workflow asks for.
4. The runner now shows as **Idle** under Settings → Actions → Runners.

## How the Windows job finds Visual Studio

The step runs as a **batch (`cmd`) script**: `vswhere.exe` (always installed with Visual Studio) finds the newest VS 2022 with the x64 C++ tools, and `vcvars64.bat` loads its developer environment (`cl`, `cmake`, `ninja` on PATH), exactly like a *Developer Command Prompt for VS 2022*. Then it runs `cmake --workflow --preset windows-msvc-<config>`.

Why not PowerShell: Actions writes every step to an unsigned temporary `.ps1`. If the PC's execution policy is `AllSigned` (or set by Group Policy), PowerShell refuses to run it (`… is not digitally signed`). Batch files aren't covered by the execution policy, so the job works without loosening that setting.

## Day-to-day

- **Turn it off when you don't want builds:** `Stop-Service "actions.runner.*"` (or Services → *GitHub Actions Runner* → Stop / Startup type *Manual*). Windows jobs then wait in the queue (up to 24 h) until you start it again.
- **Re-run a job:** PR → *Checks* → *Re-run jobs*. **Run on any branch manually:** *Actions → CI → Run workflow*.
- **Updates:** the runner updates itself; keep Visual Studio updated.
- **Remove the runner:** `.\config.cmd remove --token <removal token from the Runners page>`.

## Even safer (optional)

- Run the runner inside a **Hyper-V VM** (or a separate PC) instead of on your main Windows install. That's full isolation, but GPU tests (later milestones) then need GPU passthrough.
- Make the repo **private** while it's early: then fork PRs can't exist at all. (Private repos have limited free hosted-runner minutes; the self-hosted runner is unaffected.)
