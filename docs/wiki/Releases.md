# Releases

Every version tag (`v0.2.0`, …) gets a **GitHub Release** with `Meow2-<version>-windows-x64.zip`. Players unpack it anywhere and run it; nothing needs installing. The release notes are that version's section of [CHANGELOG.md](../../CHANGELOG.md) (techstack.md §21, AGENTS.md §2.7).

- Status: ✅ M1 (#76). The first release made this way is `v0.2.1` (`v0.2.0`'s run failed on a workflow bug, #84).

## What's in the zip

One folder, `Meow2-<version>-windows-x64/`, with:
- `Meow2Player.exe`, `Meow2Renderer.exe`, `Meow2Editor.exe`: Release builds, no console window ([Building](Building.md#release-builds-without-a-console));
- their **`.pdb` files**, so a crash dump (`.dmp`) a player sends from that build can be opened with full call stacks ([CrashDumps](CrashDumps.md));
- the **MSVC runtime** (`vcruntime140.dll`, `msvcp140.dll`, the Universal CRT `api-ms-win-*.dll`, …), app-local, so no "Visual C++ Redistributable" installer is needed;
- **Qt** for the editor (`Qt6*.dll`, `plugins/`, `qt.conf`), copied by Qt's deploy script (windeployqt);
- `README.md`, `LICENSE`, `CHANGELOG.md`.

## Making a release (owner)

1. **Close the milestone in a PR:** it bumps `project(Meow2 VERSION …)` and turns `[Unreleased]` in `CHANGELOG.md` into `[0.Y.0] — <date>` (AGENTS.md §2.7). Merge it once CI is green.
2. **Tag the merge commit on `main` and push the tag** (only the owner, or an agent with the owner's go-ahead):
   ```
   git fetch origin
   git tag -a v0.2.0 -m "Meow2 0.2.0" origin/main
   git push origin v0.2.0
   ```
3. **`.github/workflows/release.yml` runs:**
   - **`package`** (your self-hosted Windows runner, read-only token):
     - builds and tests the `windows-msvc-release` preset;
     - builds the zip;
     - checks the zip's name matches the tag;
     - unpacks it into an empty folder and runs every app there (offscreen, 3 frames);
     - uploads it as an artifact.
   - **`publish`** (a GitHub-hosted runner, the only job with write access): takes the zip, cuts `## [0.2.0]` out of `CHANGELOG.md` and creates the Release.
4. **Check the Release page:** download the zip on a clean folder or PC and start the apps.

If a step fails, nothing is published. Tags are never moved or deleted, so:
- **The runner had a problem** (offline, out of disk): re-run the workflow from the Actions tab.
- **The fix needs a change** to the code, a test or `release.yml` itself: a re-run can't pick it up, because the workflow always runs the files of the **tagged commit**. Fix it on `main`, then make a PATCH release (below).

## Patch releases

A fix for an already-tagged version ships as `0.Y.Z+1` from a `release/0.Y` branch (techstack.md §21):

1. Create the branch from the tag once: `git push origin v0.2.0:refs/heads/release/0.2`.
2. A `fix/` PR **into `release/0.Y`** carries the fix (cherry-picked from `main`), bumps `project(Meow2 VERSION …)` to the next PATCH and adds a `## [0.Y.Z] — <date>` section to `CHANGELOG.md` (that section becomes the release notes).
3. After merging it, tag the merge commit **on `release/0.Y`** and push the tag; `release.yml` runs as above.
4. `main` gets the same fix in its own PR, and its `CHANGELOG.md` gets the same `[0.Y.Z]` section, so the history matches.

Example: `v0.2.0`'s run built the zip, then stopped on a cmd parsing bug in `release.yml` (#84), so the first published release is **`v0.2.1`**.

## Building the zip locally

```
cmake --workflow --preset windows-msvc-release                       # build + test
cmake --build --preset windows-msvc-release --target package         # -> build/windows-msvc-release/package/
cmake -DMEOW2_ZIP=build/windows-msvc-release/package/Meow2-0.1.0-windows-x64.zip ^
      -DMEOW2_WORK_DIR=%TEMP%/meow2-package-test -P cmake/TestPackage.cmake
```

The last command does the same check as the release workflow. Run it from a plain prompt (no developer prompt, no Qt on `PATH`): then a DLL missing from the zip can't be found elsewhere by accident.

## How it works

- **Install rules:**
  - each app calls `meow2_install_app(<target>)` (`cmake/Meow2Packaging.cmake`): the exe and its PDB go into the zip's top folder;
  - the editor then runs `qt_generate_deploy_app_script(… DEPLOY_TOOL_OPTIONS --include-plugins qoffscreen …)`;
  - `meow2_package()` at the end of the top-level `CMakeLists.txt` adds the runtime (`InstallRequiredSystemLibraries` with the UCRT), the documents and the CPack settings (ZIP, file name, one top-level folder).
  - Install rules run in the order they're declared, so the editor is installed before Qt's script looks at it.
- **PDBs in Release:** every configuration compiles with `/Zi`, and Release links with `/DEBUG /OPT:REF /OPT:ICF` (top-level `CMakeLists.txt`). `/DEBUG` writes the PDB; `/OPT:REF,ICF` keep removing unused and duplicate code, which `/DEBUG` would otherwise switch off, so Release is as small and fast as before. The PDB isn't loaded at run time; it only matters when someone opens a crash dump.
- **Release notes:** `cmake/ReleaseNotes.cmake` cuts one version's section out of `CHANGELOG.md`, keeping blank lines and Markdown. It fails if the section is missing or empty, so a version can't be released without notes (tests: `Build.ReleaseNotes.*`).
- **Security:**
  - the self-hosted job only ever has a read-only token, and the write token lives on a GitHub-hosted runner;
  - only tags in this repository trigger the workflow;
  - only GitHub's own actions are used, pinned to commit SHAs;
  - no secrets.

  See [CI](CI.md) and AGENTS.md §5.

## Not yet

- Linux and Android packages (M13).
- An installer (MSI) or code signing.

See also: techstack.md §21, [Building](Building.md), [CI](CI.md).
