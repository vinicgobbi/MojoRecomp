# MojoRecomp Agent Guide

This file defines the operating rules for AI coding agents working in this
repository. It is intentionally focused on decisions and guardrails. User-facing
setup and product information belongs in `README.md`.

## Project identity and scope

- The project name is **MojoRecomp**. The desktop application is
  **MojoRecomp Launcher**.
- Do not introduce or restore the former project name in code, paths, UI,
  metadata, package names, documentation, or generated output.
- MojoRecomp is an independent native recompilation project for Xbox 360 Crash
  titles. It is not affiliated with or endorsed by the games' rights holders.
- **Crash of the Titans (COT)** is the current playable target.
- **Crash: Mind Over Mutant (MOM)** has a separate launcher profile and runtime
  identity, but is not yet a playable target.
- Supported host platforms are **Windows x64** and **Linux x64**. Both the
  launcher and the runtime are being made native on each platform. Windows is
  the established release target; Linux support is in progress and must not be
  described as released or validated until it has passed the same manual
  gameplay and visual review. No other host OS or architecture is in scope.
- Keep maintained source code, project-controlled identifiers, comments, UI
  text, configuration, tests, logs, and documentation in English.
- English is mandatory for class, function, method, variable, constant, field,
  enum, test, file, and directory names created by the project; comments;
  diagnostics; console/log output; test fixtures and synthetic path names; build
  scripts; and developer-facing messages. Do not introduce Portuguese or any
  other non-English prose into maintained code merely because the project owner
  speaks that language. Non-English text is allowed only when it is intentional
  localization content or a proper name/title that must retain its original
  spelling.
- Do not use emoji or decorative Unicode glyphs in maintained source code,
  identifiers, comments, tests, logs, diagnostics, scripts, configuration, or
  developer-facing runtime/build output. Prefer plain ASCII punctuation there
  unless a non-ASCII character is required by an intentional localization or
  character-encoding test. Markdown documentation may use emoji or Unicode
  symbols when they improve readability, such as status indicators or checkmarks.

## Non-negotiable Git and collaboration rules

Read-only Git commands such as `git status`, `git diff`, `git log`, and
`git ls-files` are allowed. Every state-changing or remote Git action requires
an explicit instruction from the project owner for that exact action.

- Never run `git add`, `git commit`, `git push`, `git tag`, or publish a release
  unless explicitly requested.
- Never create an issue or pull request. If the owner later wants one, prepare a
  local summary or patch for human review instead of publishing it. Do not
  contact upstream projects or their maintainers on your own.
- Never force-push or change Git remotes.
- Do not create or switch branches/worktrees, stash changes, merge, rebase,
  amend, reset, restore, checkout files, or clean the worktree unless the owner
  explicitly requests that operation.
- A request to "finish", "fix", "update", or "clean up" is not authorization
  to stage, commit, push, open a PR, or modify the index.
- Preserve unrelated user changes. Inspect the relevant diff before editing and
  never discard work merely to obtain a clean status.
- The historical Git index is protected and may retain references to removed
  private or diagnostic material. Do not publish, commit, rewrite, unstage, or
  clean that state without a specific owner-approved plan.
- If a future request explicitly authorizes a commit, first inspect the exact
  staged set and ensure the protected historical index and private material are
  excluded. Do not assume the existing index is safe to commit.

## Legal and data boundary

The repository must remain useful without distributing copyrighted game data.
Users provide their own legally obtained supported game input.

Never add, stage, package, upload, quote, or expose:

- Xbox 360 disc images, archives, executables, title updates, keys, decrypted
  intermediates, movies, audio, textures, or other original game assets;
- extracted or managed game directories, including `games/`;
- saves, settings, user content, caches, or any `userdata/` contents;
- generated PPC translation units or generated guest images;
- build directories, packaged applications (including AppImages), executables,
  DLLs, shared objects, symbols, maps, crash dumps, core dumps, shader dumps, logs, screenshots, captures, or support bundles;
- local toolchain/dependency trees; or
- `.private/` contents, agent handoffs, operational notes, or other private
  development state.

Do not use real user saves or installed game data in automated tests. Use
isolated temporary fixtures and synthetic inputs. Do not weaken ignore rules or
release validation to make a local build artifact appear publishable.

Third-party license and notice files are required compliance material, not
documentation clutter. Preserve them and their provenance. Original MojoRecomp
code is licensed under the ISC License in the repository root unless an
individual file states different terms. Do not apply that ISC grant to
third-party code/tools, generated guest/PPC or original game code, game data,
launcher game artwork, names, characters, logos, trademarks, or any material
owned by another rights holder.

## Repository map and ownership

- `launcher/`: MojoRecomp Launcher, built with Tauri, Rust, Svelte, TypeScript,
  and Vite. It owns setup/import, validation, settings, launch/monitoring,
  updates, logs, support tooling, and title selection.
- `runtime/`: native C++ host/runtime for the recompiled guest. It owns kernel
  services, VFS, timing, threads/fibers, input, audio, video, and Vulkan/Xenos
  presentation. Runtime tests live under `runtime/tests/`.
- `config/`: maintained XenonRecomp configuration and switch tables. Treat these
  inputs, not generated PPC output, as the source of truth for recompilation.
- `patches/`: reviewed patches for pinned XenonRecomp, XenosRecomp, and other
  upstream dependencies that require MojoRecomp compatibility changes.
  Update the maintained patch instead of hand-editing a disposable dependency
  checkout.
- `tools/`: analysis, recompilation, and asset-conversion helpers. Maintained
  tools are cross-platform Python programs invoked directly; see
  "Cross-platform tooling policy".
- `launcher/scripts/`: cross-platform Node.js build, notice, packaging, and
  release-validation scripts exposed through `launcher/package.json`.
- `launcher/src-tauri/src/platform.rs`: single owner of launcher OS differences
  (executable names, required runtime files, release targets, catalog names,
  process and self-replacement details).
- `launcher/resources/`: title metadata, required notices, verified license
  texts, and distribution metadata. Do not delete license files to reduce file
  count.
- `ppc/`: local generated guest code. It is not maintained public source.
- `thirdparty/`: pinned upstream source dependencies are tracked as Git
  submodules. Generated build/work trees and the local LLVM toolchain are
  ignored. Keep submodules pristine; permanent MojoRecomp changes belong in
  `patches/`, project build wrappers, or maintained source outside the submodule.
- `.private/`: ignored local operational state. It must never enter public
  source, patches, logs, reports, prompts, or release artifacts.
- `version.toml`: source of truth for suite, launcher, and per-title versions.
  Release bumps write it; see "Versioning and commit messages".
- `.cz.toml`, `.cz.<component>.toml`, `tools/commitizen_mojorecomp/`: commit
  message rules, per-component release lines, and the Commitizen plugin that
  attributes commits to components and writes versions.
- `.github/workflows/`: CI for commit message checks, release bumps, and the
  launcher release build.
- `launcher/CHANGELOG.md`, `runtime/CHANGELOG-COT.md`: generated changelogs.
- `docs/runtime-port/`: the native Windows/Linux runtime port plan, its work
  packages and decisions, and the inventory of Windows dependencies. Update
  the package status and inventory in the same change that advances a
  package.

Avoid adding planning diaries, migration reports, agent handoffs, or duplicate
status Markdown files. Update `README.md` or this file only when the information
belongs there, and create new documentation only when the owner asks for it.

## Versioning and commit messages

- Every commit message follows Conventional Commits:
  `type(scope): summary`, with an optional `!` or `BREAKING CHANGE:` footer.
  CI rejects other messages (`.cz.toml`). The type decides the bump: `feat`
  is minor, `fix`, `perf`, and `refactor` are patch, breaking changes are
  major (minor while a line is still `0.x`). Other types do not release.
- Each release line is defined in `.cz.<component>.toml`: the launcher (tags
  `vX.Y.Z`, also drives the suite version) and the COT runtime (tags
  `cot-runtime-vX.Y.Z`, currently SemVer pre-releases such as
  `0.2.0-alpha.1`). A commit belongs to every line whose
  `extras.component_paths` it changes. The scope is descriptive only and never
  decides the component.
- The release-bump workflow bumps each line on pushes to `main`, regenerates
  its changelog, and pushes the release commit and tag. Do not edit release
  versions or generated changelog entries by hand; fix the commit history or
  configuration instead.
- A new launcher tag is built for Windows (portable `.exe`) and Linux
  (AppImage) and published as a GitHub release by `launcher-release.yml`.
  The COT runtime is built from the user's own game files, so it is never
  built in CI: its release (`cot-runtime-vX.Y.Z`) is produced locally with
  `release.bat` and published by the owner. Each release publishes complete
  update catalogs and carries the other component's published entries.
- These workflows are the owner-approved publishing path. Agents still never
  push, tag, or publish releases themselves without an explicit request.
- Releases are currently unsigned: `launcher/release-signing-public-key.hex` is
  empty, which selects the launcher's unsigned release mode. Putting a public
  key back, with the private key supplied through
  `MOJORECOMP_RELEASE_SIGNING_KEY`, restores signed releases. Do not treat the
  unsigned mode as permanent or weaken it further.
- `version.toml` stays the source of truth. Any other file that embeds a
  component version must be listed in that line's `extras.version_mirrors`.
  The README download link and status table are updated when a release is
  published, not by the bump.
- MOM gets its own release line and tag prefix when it becomes a release
  target. Do not let it inherit the COT line.

## Architecture rules

- Prefer general, title-independent runtime behavior over scene-, address-,
  ordinal-, vendor-, or machine-specific hacks.
- Keep title-specific hooks and addresses explicit and isolated. Do not make MOM
  silently inherit COT behavior; share code only after both titles prove the
  same stable contract.
- Do not manually edit generated PPC output when a configuration, generator,
  patch, or runtime fix is the durable source-level solution.
- Keep guest simulation/timing independent from host rendering performance. COT
  remains a 30 FPS title unless a separately designed and validated feature
  changes that contract.
- Preserve game behavior and visual intent. A change that fixes one scene while
  regressing another is not complete.
- Hardware behavior claims require evidence. Form a hypothesis, reproduce the
  issue, prefer an isolated probe/test, and validate representative game scenes.
- Unsupported GPU capabilities must fail or fall back safely; do not infer
  capabilities from vendor names alone.
- Launcher updates must preserve the configured game library and the separate
  Saved Games and Local AppData trees. Legacy portable `games/` and `userdata/`
  directories are migration inputs only and must never be discarded silently.
  Uninstall and cleanup paths must make any user-data deletion explicit and
  narrowly scoped.
- Do not replace the currently owner-approved launcher artwork or remove its
  hash/provenance validation without explicit owner approval.

## Platform rules

- Keep OS-specific code behind explicit, isolated boundaries: `platform.rs` and
  `#[cfg(...)]` blocks in the launcher, and host abstraction layers selected by
  CMake platform checks in the runtime. Shared logic must stay platform-neutral
  and must not branch on OS in scattered call sites.
- Every launcher change must keep both Windows and Linux building. The runtime
  port to Linux is in progress: runtime changes must not regress the Windows
  build, and new runtime code should be written against the host abstraction
  rather than adding new direct Win32 dependencies to shared code. Never add a `cfg`/`#ifdef` path for one platform without the matching path,
  or an explicit and safe "unsupported" failure, for the other.
- The Linux runtime is native. Do not add Wine, Proton, or other Windows
  compatibility-layer bridges, settings, or fallbacks.
- On Linux the launcher is distributed as an AppImage and self-updates by
  replacing that AppImage. The Windows package layout and update catalog name
  must stay compatible with launchers that are already installed.
- Linux user data uses the existing XDG-based fallback (`XDG_DATA_HOME`,
  defaulting to `~/.local/share`). Do not introduce a new layout without owner
  approval, and keep it covered by the same migration and preservation rules
  as Windows.
- A launcher on a platform for which no runtime package has been published yet
  must report that clearly and degrade gracefully instead of failing obscurely
  or downloading a package built for another OS.
- Treat paths, case sensitivity, path separators, executable bits, line
  endings, and process/signal semantics as platform differences. Use the
  standard library or the toolchain for path handling instead of hand-built
  separators or drive-letter assumptions.

## Cross-platform tooling policy

The goal is a single set of native, cross-platform entry points with no shell
wrapper scripts. The legacy Windows batch files (`setup.bat`,
`build-smoke.bat`, `release.bat`, `build-localization-delta.bat`, and
`tools\*.bat`) are being retired.

- Never add a new `.bat`, `.cmd`, `.ps1`, or `.sh` project script, and never
  restore one that has been removed. Do not invoke or document `.ps1` scripts.
- Implement automation in the layer that owns it:
  - runtime configuration, build, dependency/toolchain detection, and tests:
    CMake (presets, options, `find_*` logic, and CTest);
  - analysis, recompilation, patch application, and asset conversion: Python 3
    programs under `tools/`, invoked directly with the platform's Python 3
    interpreter;
  - launcher build, notices, packaging, and release validation: Node.js
    scripts under `launcher/scripts/`, exposed as `npm run` scripts;
  - end-user operations such as import, extraction, validation, and launch:
    native Rust in the launcher.
- Native entry points must work on Windows and Linux, be non-interactive, fail
  with a non-zero exit code and a clear message, and handle repository paths
  that contain spaces. Do not hard-code machine-specific install locations
  such as `C:\Program Files\...`; discover tools or accept them as explicit
  configuration.
- `npm run` scripts must call `npm`/`node` portably, never `npm.cmd`.
- When replacing a batch file, move its full behavior into the owning native
  entry point, update `README.md`, this file, and any callers in the same
  change, and only then delete the batch file. Until a replacement exists, the
  legacy batch file remains the Windows entry point and must keep working.
- Do not duplicate build logic between a legacy batch file and its native
  replacement. While both exist, the batch file may only forward to the native
  entry point.

## Canonical commands

Run commands from the repository root unless a command changes directory.

Launcher checks and production build (Windows and Linux):

```sh
cd launcher
npm ci
npm run check
npm run generate:notices
cargo test --manifest-path src-tauri/Cargo.toml
npm run build:production
```

Python tools are invoked directly, for example `python3 tools/recompile.py`
(`py -3 tools\recompile.py` on Windows).

Legacy Windows-only entry points, kept until their native replacements land:

```bat
setup.bat
tools\analyze.bat
tools\recompile.bat
build-smoke.bat
release.bat          (COT runtime release candidate)
```

When a native replacement lands, replace its entry here instead of listing
both.

Public release packaging requires the release asset base and notes URLs. Artwork,
clean-install behavior, and representative gameplay/visual behavior remain manual
release review responsibilities and must be reported accurately rather than encoded
as environment-variable acknowledgements.

Do not run setup, dependency installation, a full rebuild, packaging, or the
entire test suite by habit. Use the smallest command that meaningfully validates
the changed area, and state clearly when a required check could not be run.

## Change workflow

1. Read this file and the relevant section of `README.md`.
2. Inspect `git status` and the relevant diff without altering the index.
3. Identify the maintained source of truth before editing. Generated output,
   local dependency copies, and packaged files are usually not that source.
4. Make the smallest coherent change and preserve unrelated work.
5. Add or update an isolated regression test/probe when behavior changes and a
   stable automated assertion is possible.
6. Run focused validation for the affected layer.
7. Review the final diff for private data, game assets, generated output,
   obsolete naming, accidental license changes, and unrelated edits.
8. Report what changed, what was validated, and what still requires manual
   verification. Do not stage or commit the result.

## Validation by area

- Platform coverage: validate on the host you are running on and state
  explicitly which platform was not built or tested. For launcher Rust changes
  that touch `cfg` code, also type-check the other target when its toolchain
  is available (for example `cargo check --target x86_64-pc-windows-msvc` or
  `--target x86_64-unknown-linux-gnu`); otherwise report it as unverified.
- Runtime C++ or CMake: run the relevant focused test/probe, then the smoke
  build (currently `build-smoke.bat` on Windows) when the change can affect
  integration or linking.
- Recompiler configuration or switch tables: run analysis, recompilation, and
  the relevant smoke gate. Generated output remains local.
- Launcher Svelte/TypeScript/CSS: run `npm run check`; build the launcher
  when bundling or runtime integration is affected.
- Tooling migration (replacing a batch file): run the new native entry point
  on every platform available to you and confirm it reproduces the batch
  file's behavior before deleting the batch file.
- Launcher Rust/Tauri: run focused Cargo tests, then the launcher build when
  IPC, setup, filesystem, process, or packaging behavior changes.
- Versions, bundled resources, notices, or packaging: regenerate notices if
  dependency inputs changed and run the repository's release validation/build
  flow. Never solve a validation failure by removing required notices or guards.
- Documentation-only changes: inspect links, paths, command names, naming, and
  consistency; do not trigger unrelated heavy builds.

Automated tests do not replace manual gameplay and visual validation. Manual
visual validation is still pending for the current release state. Do not claim
that rendering, aspect ratios, UI presentation, or representative gameplay are
fully approved solely because compilation or probes passed.

## Completion criteria

A task is complete only when the requested behavior is implemented at the
correct source layer, scoped checks pass (or failures are reported honestly),
the diff contains no private/game/generated/build material, and remaining manual
validation or licensing decisions are named explicitly. Do not claim release
readiness while required manual visual/gameplay validation remains pending.
