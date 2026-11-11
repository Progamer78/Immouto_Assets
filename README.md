# Monochrome native iOS port

Work in progress: this is the native runtime project for the game specified in [TASK.md](TASK.md). **The user has installed the full IPA and played on an iPhone. The complete game remains unfinished.** Opening, home, native save/load, Training and Reading pass locally. The first device feedback identified interaction, dialogue alignment, launch and touch-control issues; fixes now pass local checks. See [PORT_STATUS.md](PORT_STATUS.md) for evidence and remaining limitations.

The supplied game files remain in their original directory. Git excludes the original game, generated game data, private asset archives, build outputs and signing material.

For the IPA download and Windows signing steps, see [Install on iPhone](docs/IPHONE_INSTALL.md).

## Windows development

Data preparation uses Python 3.12 and Node.js 22+ to decode the supplied archives. A portable Node/compiler/build toolchain can be installed inside this workspace without a system-wide installer:

```powershell
python tools/bootstrap_windows.py
python tools/prepare_game.py
python -m unittest discover -s tests -v
powershell -ExecutionPolicy Bypass -File tools/build_windows.ps1 -App
build/windows-app/wolf-headless.exe build/game/game.json 120
```

The headless command returns a nonzero exit status on unsupported game behavior and prints the original event/command location. That is expected while command implementation is incomplete.

To prepare graphics/dialogue/audio and run the native frontend:

```powershell
python tools/prepare_game.py --stage
build/windows-app/WolfPort.exe build/game
```

The frontend renders the original title prompt, main menu, opening dialogue, choices and home screen. Recorded input advances the opening, opens Save and writes slot 2 with its original menu details and a native JPEG thumbnail. Full native save/load commands and partial saved-variable/database reads now have regression coverage. Map drawing, movement/collision and broader gameplay remain unfinished; original Windows saves are incompatible. Development snapshots use the app's storage directory; incompatible resumes are preserved separately before a fresh startup. See [reproducible local checks](docs/VALIDATION.md).

With an existing C++17 toolchain and CMake, the portable core can instead be built without SDL:

```text
cmake -S . -B build/core -DWOLF_BUILD_APP=OFF
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

## GitHub Actions from Windows

The iOS build runs on a GitHub-hosted macOS runner. You do not need a Mac.

1. Create an **empty private repository** on GitHub. Do not initialize it with a README or `.gitignore`; this workspace supplies those.
2. Push this workspace's source Git repository. The original game and `artifacts/` are excluded automatically. The native-check workflow runs on source pushes.
3. To package the game, run `python tools/prepare_game.py --zip`. This resolves the original `.wolf` archives, then creates `artifacts/game-assets.zip` with the converted data and original Data resources. It omits Windows executables, the supplied saves and duplicate archives. The runtime needs neither Node nor the archive decoder.
4. In the private repository's **Releases**, create a release with tag `game-assets`. Attach `artifacts/game-assets.zip` to the release. Upload it as a **release attachment**, not through the repository's Add file screen. The asset stays outside Git history.
5. Open **Actions -> Build iOS IPA -> Run workflow**. Leave `include_game` unchecked for an initial compilation check; enable it to build with your private assets. Leave `signed` unchecked unless signing is configured.
6. After a successful run, open **Releases** and download the IPA from `ios-build-...`. The large IPA is a release attachment, not an Actions artifact. An unsigned IPA requires signing before installation. A small Actions artifact contains build-tool metadata.

GitHub allows release attachments below 2 GiB each, which accommodates the supplied game. Source checks do not download the asset package; only manually requested full iOS builds do. The asset ZIP and resulting IPA stay outside Git history and Actions artifact storage. Small build metadata artifacts are retained for seven days. Private-repository compute usage depends on your GitHub plan; source checks and unsigned iOS builds work without Apple signing secrets.

The source is connected to the private [Progamer78/Immouto_Assets repository](https://github.com/Progamer78/Immouto_Assets). The approved game package is uploaded as a [private release attachment](https://github.com/Progamer78/Immouto_Assets/releases/tag/game-assets), with its remote SHA-256 verified. Do not upload the whole original folder through Git, and do not commit the asset ZIP or signing files.

The first [unsigned iOS check](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585937560) completed in 4 minutes 1 second, including setup, compilation, packaging and publication. Its [release](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-1-1) is a compilation-check artifact, not a playable game build. The downloaded IPA was checked for a valid ZIP, ARM64 executable, bundle identifier and iOS deployment target. Details are in [reports/ios_build_check.json](reports/ios_build_check.json).

The opening-runtime [iOS check](https://github.com/Progamer78/Immouto_Assets/actions/runs/42634139411) for `59133fc` passed in **48 seconds overall**, with 6 seconds of configuration and 17 seconds of compilation. Its [958 KB unsigned IPA](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-3-1) was downloaded and checked for ZIP integrity, digest and ARM64 metadata. These timings exclude game assets and signing; that build predates the later arithmetic-range/shake fixes and does not validate device gameplay.

The home/save/load [iOS check](https://github.com/Progamer78/Immouto_Assets/actions/runs/42678866407) for `c6bbbcf` passed in **44 seconds overall**, including 5 seconds of configuration and 18 seconds of compilation. The [995,640-byte unsigned IPA](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-4-1) passed ZIP/digest and ARM64 metadata verification. It contains no game assets and has not been installed on an iPhone.

The latest **[movable-controls update, build 9](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-9-1)** is about 1 MB and built in **39 seconds**. Tap the floating three-dot button to show/hide the controls, or drag it to reposition it. Hidden controls give the game more room while preserving its aspect ratio. This update also fixes empty choice rows, Cancel/right-click input, and additional audio operations used around returning home. The native Day 2 return-home path passes. Install over the existing full build using the same signing identity; do not uninstall the app or delete its assets/saves. See [update instructions](docs/IPHONE_INSTALL.md). New phone verification remains outstanding.

The first **[full asset IPA](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-5-1)** for `a399fb9` built successfully in **2 minutes 45 seconds**, including asset download, compilation, packaging and publication. Download `Monochrome-unsigned.ipa` (about 1 GB), then follow [the Windows installation guide](docs/IPHONE_INSTALL.md). This development build includes the original game assets but is unsigned; the user confirmed installation and gameplay on an iPhone, with compatibility and interface problems now under repair.

## Fast development cycle

Most interpreter fixes are built and tested on Windows first. The iOS workflow caches SDL source, compiled dependency objects and CMake configuration results with an Xcode-version/CMake key; it does not cache or download the approximately 1 GB game package for source-only builds. Documentation-only pushes do not rerun desktop compilation, and a newer source push cancels superseded native checks.

For repeated iPhone testing, install one full build with `include_game=true` and `reuse_assets=true`. On first launch it copies the versioned assets into app storage, displaying progress. Subsequent builds can use `include_game=false`, `reuse_assets=true`: they contain the runtime and reuse that installed data. Use the same app identifier/signing setup and **update the existing app without deleting its data**. If a sideloading method removes the app first, its data may need reinstalling too.

This development mode temporarily keeps a bundled and a persistent copy on the first full install. Later thin updates remove the bundled copy. For a complete standalone build that reads bundled assets directly, use `include_game=true`, `reuse_assets=false`. Asset copying/reuse is tested on Windows; iPhone install/update behavior and full-package Actions timings remain unverified. The measured cached source-only check took 48 seconds.

On iPhone, the app's folder is exposed in **Files -> On My iPhone -> Monochrome**. `last-error.json` records event/command failures and original byte offsets; `crashes.txt` records best-effort fatal-signal breadcrumbs. The event diagnostic is tested locally. Native crash logging and Files access still need device testing. Windows diagnostics use SDL's preference directory; developers can set `WOLF_STORAGE_DIR` to a workspace directory while testing.

## Optional Apple signing

To export a directly installable development/ad-hoc build, use an Apple certificate and a matching provisioning profile that includes the target iPhone. Configure these repository **Actions secrets**:

| Secret | Value |
| --- | --- |
| `IOS_CERTIFICATE_P12_BASE64` | Base64-encoded signing certificate `.p12` |
| `IOS_CERTIFICATE_PASSWORD` | Password protecting the `.p12` |
| `IOS_PROVISIONING_PROFILE_BASE64` | Base64-encoded provisioning profile |

Set repository **Actions variable** `IOS_BUNDLE_ID` to the identifier allowed by that profile. It defaults to `com.local.monochromeport`. The workflow derives the team and export method from the profile, imports credentials into a temporary runner keychain, and removes that keychain afterward. Never send signing secrets in chat or place them in source files.

Windows can convert an existing certificate/profile file to Base64 using:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes('C:\path\certificate.p12')) | Set-Clipboard
```

Use the same command with the profile's path for the profile secret. These signing steps are for later device testing; they do not make the incomplete runtime playable.

## Technical notes

- [Architecture and evaluated alternatives](docs/ARCHITECTURE.md)
- [Original-data intermediate format](docs/DATA_FORMAT.md)
- [Third-party licenses](THIRD_PARTY_NOTICES.md)
- [GitHub release attachment limits](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)
