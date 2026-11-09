# Native port status — 2026-11-09

**In progress. The game is not playable yet. Desktop GitHub Actions checks pass, and an unsigned ARM64 iOS compilation-check IPA has been built and inspected.**

## Confirmed source

- Living with Sister: Monochrome Fantasy, settings subtitle `+ DLC v2.02`.
- `Game.exe`: WOLF RPG Editor file version `3.191.2023.529`; no GamePro.exe found.
- Supplied directory: `Living_With_SIster_Monochrome_Fantasy_Kagura_Decypted/Living With SIster Monochrome Fantasy`.
- 1024×576, 60 FPS, 16-pixel tiles configured in Game.dat.
- 550 common events, nine maps, six tilesets; 40 user / 30 mutable / 28 system database types.
- 205,864 command records and 51 distinct IDs across common and map events. 107 common events are configured for parallel execution.
- Loose resources coexist with BasicData.wolf and MapData.wolf. Original/archive precedence and equivalence are not verified. MTool executables/saves are present and are excluded from the runtime build.
- Original files have only been read/copied. Key source SHA-256 values are recorded in [reports/game_inventory.json](reports/game_inventory.json).

## Architecture and implemented pieces

Focused C++17 interpreter, original-data conversion with Python, SDL2 desktop/iOS frontend, CMake-generated Xcode project. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for alternatives and pinned references.

- Strict, complete-consumption readers for Game.dat, CommonEvent.dat, nine `.mps` files, three databases and their `.project` schemas, and TileSetData.dat.
- Original commands, argument bits, strings, pages, routes and database metadata preserved in the [documented IR](docs/DATA_FORMAT.md).
- Variable banks, per-map event-self identity, database addressing, common-event call stacks, frame waits, branches, labels and counted loops have an initial native implementation.
- Development snapshots preserve all currently implemented state, including stacks, loop/wake state, databases, picture records and RNG. Atomic replacement and rejection of incompatible/corrupt state are tested. This is not yet the game's SaveLoad implementation.
- SDL frontend compiles on Windows. Basic file pictures, crops, opacity/blending, logical scaling, keyboard/controller mappings and touch controls are implemented. Full rendering fidelity is not verified.
- Persistent, manifest-identified asset cache supports one full install followed by small runtime-only development updates. Copy/reuse behavior is tested on desktop; device update persistence remains unverified.
- Event-stop diagnostics, best-effort fatal-signal breadcrumbs and an iOS Files-accessible diagnostic directory are implemented. Actual iOS crash reporting is unverified.

## Command coverage

These are **partial implementations**, not a claim that every variant of an ID works:

| IDs | Current scope |
| --- | --- |
| 0, 99, 103 | Blank rows, editor checkpoints, comments |
| 111, 401, 420, 421, 499 | Numeric comparisons and branch selection/end |
| 121 | Basic 32-bit arithmetic/assignments, random calculation, remainder/AND; range, indirection and option flags still missing |
| 122 | Literal string assignment and a subset of variable substitutions |
| 123–125 | Initial basic/mouse queries and automatic basic-key input; full input semantics missing |
| 150 | Plain picture-show records and immediate erase; advanced variants and animation missing |
| 160 | Transition settings retained; actual transition rendering missing |
| 170–172, 176, 179, 180, 498 | Loops, break/end, counted iterations, waits |
| 175, 210, 212, 213, 300 | Quit, common-event calls, labels/jumps; some call variants missing |
| 250 | Basic field read/write and explicit type/row/field name lookup; other operations missing |

Every unsupported command/layout stops and records its context; it is not silently skipped. Full WOLF scheduling order/cancellation, system-variable defaults and blocking rules still need reference validation.

## Verification and current incompatibility

- Python tests: **11 passing**, including supplied-game structure checks and malformed-data/build-helper checks.
- Native runtime: **7 passing scenarios** covering waits, branches/loops, independent self banks, state persistence, incompatible saves, unknown commands, database name lookup and full-install/thin-update asset reuse.
- Windows native application and headless runtime compile using a workspace-local Zig/CMake/Ninja toolchain.
- GitHub workflow syntax checked with checksum-verified actionlint 1.7.7.
- First remote [native check run](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585759780) passed: Windows, Linux and macOS core tests, plus macOS SDL frontend compilation. It completed in approximately 65 seconds including runner setup.
- The real startup interpreter executes **743 command records**, then stops in common event **48** at command **201**, original CommonEvent.dat byte offset **114829**. See [reports/startup_trace.json](reports/startup_trace.json).
- This is **before a verified title screen**. No original Windows gameplay session, native gameplay loop or iPhone test has been performed.

## iOS and fast build cycle

- Manual GitHub macOS workflow generates an ARM64 iOS 15+ Xcode project; unsigned packaging and optional Apple-profile-based signed export are configured.
- Source checks run without downloading the game. iOS dependencies have a toolchain-keyed Actions cache.
- Source is tracked separately from the approximately **989 MB** private game ZIP. Large asset packages and IPAs use private Release attachments, not Git history or Actions artifacts.
- Full development install: `include_game=true`, `reuse_assets=true`. Later small updates: `include_game=false`, `reuse_assets=true`, preserving the existing app/data and identifier.
- Standalone bundled mode: `include_game=true`, `reuse_assets=false`.
- Source checkpoint `8fa13c8` is uploaded to the private [Progamer78/Immouto_Assets repository](https://github.com/Progamer78/Immouto_Assets), branch `master`. The asset ZIP remains local. No signing secrets are configured or embedded.
- First unsigned, source-only [iOS run](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585937560) passed in **241 seconds overall** (236 seconds for the job). Xcode/SDL configuration took 213 seconds; actual application compilation took 10 seconds. This is not a full-game or signed-build timing.
- Produced and downloaded `Monochrome-unsigned.ipa`: **555,314 bytes**, ARM64 Mach-O executable, bundle ID `com.local.monochromeport`, minimum iOS 15.0. ZIP integrity and bundle/executable metadata verified; no game assets included and no device installation tested. See [reports/ios_build_check.json](reports/ios_build_check.json).
- The private [release](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-1-1) contains this compilation-check IPA. Dependency caching worked; CMake-result caching was added after this baseline and needs a subsequent run to measure its effect.

## Next work

1. Implement the actual startup movement-route command and continue the trace through remaining initialization requirements.
2. Establish a separate Windows reference copy/session and resolve loose/archive behavior.
3. Implement picture variants, text/font escapes, messages/choices, database utilities, audio and remaining commands from the actual inventory.
4. Validate the title flow, new game and a complete gameplay loop against Game.exe; add game-command save/load rather than relying on development snapshots.
5. Validate the expanded CMake cache, measure full/thin build times, and validate signing, installation, updates, input, lifecycle, audio, memory and performance on iPhone.
6. Extend compatibility testing across progression and endings. Old asset-cache versions and renderer texture-cache limits still need cleanup policies.
