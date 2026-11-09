# Native port status — 2026-11-09

**In progress. The game is not playable yet. Desktop GitHub Actions checks pass, and an unsigned ARM64 iOS compilation-check IPA has been built and inspected.**

## Confirmed source

- Living with Sister: Monochrome Fantasy, settings subtitle `+ DLC v2.02`.
- `Game.exe`: WOLF RPG Editor file version `3.191.2023.529`; no GamePro.exe found.
- Supplied directory: `Living_With_SIster_Monochrome_Fantasy_Kagura_Decypted/Living With SIster Monochrome Fantasy`.
- 1024×576, 60 FPS, 16-pixel tiles configured in Game.dat.
- **600 common events**, nine maps, six tilesets; 40 user / 30 mutable / 28 system database types.
- **262,338 command records / 52 IDs** across common and map events. 120 common events are configured for parallel execution.
- **Data correction:** the supplied archives take priority over loose folders, verified with a title-marker control in an isolated Windows copy. The archives differ substantially: the previous loose-only inventory (550 events / 205,864 commands) was incomplete. Preparation now decodes and selects the archives. MTool executables/saves remain excluded.
- Original files remain unmodified: all 15 previously recorded data hashes still match. Game.exe was executed only from a separate copy. See [reference checks](reports/windows_reference_checks.json) and [selected-source inventory](reports/game_inventory.json).

## Architecture and implemented pieces

Focused C++17 interpreter, offline archive resolution with Node and binary conversion with Python, SDL2 desktop/iOS frontend, CMake-generated Xcode project. The app itself needs neither Node nor Python. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for alternatives and pinned references.

- Strict, complete-consumption readers for Game.dat, CommonEvent.dat, nine `.mps` files, three databases and their `.project` schemas, and TileSetData.dat.
- Original commands, argument bits, strings, pages, routes and database metadata preserved in the [documented IR](docs/DATA_FORMAT.md).
- Variable banks, per-map event-self identity, database addressing, common-event call stacks, frame waits, branches, labels and counted loops have an initial native implementation.
- Character property routes retain speed, appearance/animation settings, route waits and caller waits. Character movement/jumps and their rendering remain unimplemented.
- Commands 221/222 persist individual numeric/string variables in separate native files, including case-insensitive filenames, missing-file defaults, existence checks and frame-end atomic writes. Restart persistence is tested; full game SaveLoad and original Windows saves remain unsupported.
- Development snapshots preserve implemented stacks, loop/wake state, databases, picture records, delayed pictures/SE, text progress, input permissions, game clocks and RNG. Atomic replacement and rejection of incompatible/corrupt state are tested. The current development format is version 6; this is not the game's SaveLoad implementation, and playing audio positions are not restored.
- SDL frontend compiles on Windows. Basic file pictures, crops, opacity/blending, logical scaling, keyboard/controller mappings and touch controls are implemented. Full rendering fidelity is not verified.
- The original opening prompt and main title menu now render locally. Recorded input opens the menu and selects New Game. Initial text controls use the supplied font; native Ogg/MP3/WAV decoding and streamed title music use miniaudio/stb_vorbis through SDL. Text metrics, exact audio/timing behavior and broader command layouts remain incomplete.
- New Game performs its two initial immediate transfers, reaches the opening text-file notice and advances through language selection into dialogue and built-in choices. Custom windows use the original event logic, nine-slice skins, text positioning/reveal controls, picture queries and draw offsets. Character coordinates retain half-tile precision; map drawing and positional movement remain missing.
- Built-in choice windows use the original system-database skins, captured styles/positions, keyboard/controller selection and mouse/touch hit regions. Basic input permissions, mouse-device permissions and next-frame automatic basic input are retained across development resumes. Other input-device variants remain unsupported.
- Persistent, manifest-identified asset cache supports one full install followed by small runtime-only development updates. Copy/reuse behavior is tested on desktop; device update persistence remains unverified.
- Event-stop diagnostics, best-effort fatal-signal breadcrumbs and an iOS Files-accessible diagnostic directory are implemented. Actual iOS crash reporting is unverified.

## Command coverage

These are **partial implementations**, not a claim that every variant of an ID works:

| IDs | Current scope |
| --- | --- |
| 0, 99, 103 | Blank rows, editor checkpoints, comments |
| 101, 102, 111, 112, 401, 420, 421, 499 | Blocking messages and choices, gradual text reveal, numeric/string conditions and branch selection. Initial built-in choice rendering and touch hit regions; additional text/choice controls remain missing |
| 121 | Basic arithmetic/assignments, random/remainder/AND, literal/indirect operands and destinations, real intermediate calculations and result binding; ranges and advanced operators missing |
| 122 | Literal/direct/indirect string copy and append, basic deletion/replacement, UTF-8 text-file reads, missing-file and basic file-existence queries; other string operations missing |
| 123–126 | Basic/mouse queries, next-frame automatic basic input, basic-button and mouse-device permissions, picture dimensions/existence/reveal/hover queries; additional device/query variants missing |
| 130 | Immediate hero transfers with half-tile coordinates, retained/stopped map audio and cancellation of the old map's parallel runs. Transition modes and other targets remain unsupported |
| 140 | Filename SE/BGM/BGS playback, delayed SE, native pitch/mixing, streamed music and fade/loop settings; general stop/database/preload variants and saved playback position missing |
| 150 | File/text/window pictures, separate zoom axes, colors, basic show/move interpolation, delayed show/move/erase, inclusive erase/move ranges and delay reset. Free transforms, scroll links and some delayed-range interactions remain incomplete |
| 160 | Transition settings retained; actual transition rendering missing |
| 170–172, 176, 179, 180, 498 | Loops, break/end, counted iterations, waits |
| 175, 210, 212, 213, 300 | Quit, common-event calls, labels/jumps; some call variants missing |
| 201, 202 | Character property routes, waits, route replacement and waiting callers; repeating and positional routes missing |
| 221, 222 | Direct partial numeric/string save-variable reads/writes in native files; indirect access missing |
| 250 | Basic field read/write, explicit names, row/field counts, name-to-ID queries; absent numeric row reads match the verified zero result. Other utilities missing |
| 270 | Initial party image insertion/replacement/removal, erase-all and visibility state; movement/rendering fidelity unverified |
| 290 | Picture draw offsets, color flicker and initial automatic pattern variants; shake/automatic-zoom resets. Nonzero shake/automatic zoom, brightening and other effects remain unsupported |

Every unsupported command/layout stops and records its context and event stack. Execution priority follows the documented map/automatic-common/parallel-common order. Trigger re-evaluation, cancellation, many system defaults and blocking rules still need reference validation.

## Verification and current incompatibility

- Python tests: **14 passing**, including authoritative archived-game structure, archive priority/cache integrity and malformed-data/build-helper checks.
- Native runtime: **39 passing scenarios** in local Debug and/or Release builds, covering the previous title work plus transfers/map scope, nested loop breaks, message progression, database substitutions, picture queries/ranges/delays, effect separation, writable clocks, input permissions, deferred input, touch choice selection and delayed SE restoration.
- Windows native application and headless runtime compile using a workspace-local Zig/CMake/Ninja toolchain.
- GitHub workflow syntax checked with checksum-verified actionlint 1.7.7.
- First remote [native check run](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585759780) passed: Windows, Linux and macOS core tests, plus macOS SDL frontend compilation. It completed in approximately 65 seconds including runner setup.
- The title-runtime [native checks](https://github.com/Progamer78/Immouto_Assets/actions/runs/42618100010) passed for checkpoint `40dada5`: 21 runtime scenarios on Windows/Linux/macOS, archive/tool tests and macOS frontend compilation. The expanded dialogue implementation requires its own source CI check. The four supplied-game tests run locally; CI intentionally has no private game assets.
- Relative calls are now verified against Game.exe: `600100` means the current common ID; `600094` from event 270 calls event 264. A valid isolated fixture produced the expected numeric result 12.
- Authoritative startup and recorded title-menu entry both run **300 frames without a compatibility stop**. Native captures show the opening prompt, title art, menu options and font-rendered mod label. A Windows reference menu capture used an injected confirm result in the copied title prompt only. See [startup](reports/startup_trace.json), [menu](reports/menu_trace.json), [reference checks](reports/windows_reference_checks.json) and [reproduction steps](docs/VALIDATION.md).
- New Game reaches its opening notice at **550 frames without a compatibility stop**. A recorded click selects the language menu, reaches the first four-option choice at 1,200 frames, and advances through further dialogue. See [new game](reports/new_game_trace.json), [opening menu](reports/opening_menu_trace.json) and [progression](reports/opening_progression_trace.json).
- The extended replay currently stops at **frame 3161**, map event **0**, command **140**, byte **46566**, a BGM stop/fade request. This is an actual compatibility failure, not a passing full-game test. Map rendering, movement/collision, full save/load and iPhone gameplay remain unverified.
- Original-engine fixtures verified inclusive/reversed erase ranges, basic ranged movement, retained zoom, missing-picture query defaults and effects attached before a delayed show. Six text-width cases match exactly after separating rasterizer padding from logical bounds and matching per-glyph integer advances. Font hinting and broader timing fidelity remain unverified.
- A known mismatch remains when a relative range movement overlaps an already delayed relative movement (reference X=39, native X=29 in the fixture). See [reference checks](reports/windows_reference_checks.json). Sys30 currently reports simulation-tick milliseconds, without the original engine's within-frame wall-time advance.

## iOS and fast build cycle

- Manual GitHub macOS workflow generates an ARM64 iOS 15+ Xcode project; unsigned packaging and optional Apple-profile-based signed export are configured.
- Source checks run without downloading the game. iOS dependencies have a toolchain-keyed Actions cache.
- Source is tracked separately from the corrected **990,994,485-byte** private game ZIP. ZIP CRCs, archived game.json hash and its 600-event inventory were verified. Large packages/IPAs use private Release attachments, not Git history or Actions artifacts.
- Full development install: `include_game=true`, `reuse_assets=true`. Later small updates: `include_game=false`, `reuse_assets=true`, preserving the existing app/data and identifier.
- Standalone bundled mode: `include_game=true`, `reuse_assets=false`.
- Source checkpoints are uploaded to the private [Progamer78/Immouto_Assets repository](https://github.com/Progamer78/Immouto_Assets), branch `master`. The corrected asset ZIP remains local. No signing secrets are configured or embedded.
- First unsigned, source-only [iOS run](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585937560) passed in **241 seconds overall** (236 seconds for the job). Xcode/SDL configuration took 213 seconds; actual application compilation took 10 seconds. This is not a full-game or signed-build timing.
- Produced and downloaded `Monochrome-unsigned.ipa`: **555,314 bytes**, ARM64 Mach-O executable, bundle ID `com.local.monochromeport`, minimum iOS 15.0. ZIP integrity and bundle/executable metadata verified; no game assets included and no device installation tested. See [reports/ios_build_check.json](reports/ios_build_check.json).
- The private [release](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-1-1) contains this compilation-check IPA. Dependency caching worked; CMake-result caching was added after this baseline and needs a subsequent run to measure its effect.
- Expanded title-runtime [iOS check](https://github.com/Progamer78/Immouto_Assets/actions/runs/42618585767) passed for `40dada5` in **252 seconds** overall (216 configuration, 15 compilation). The downloaded [901,232-byte IPA](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-2-1) passed ZIP/digest and ARM64/bundle metadata checks. It is unsigned, contains no game assets and has not been installed on an iPhone. The later dialogue implementation is not covered by that build.
- Fixed the fallback dependency-cache prefix so a changed CMake source can reuse the existing `v2` cache. Warm build timing still requires measurement.

## Next work

1. Implement the next sound-command variants and continue the opening sequence; add actual map rendering and movement/collision.
2. Validate common-event ID queries, remaining defaults, scheduling dependencies, menu/touch interaction and rendering timing against the Windows engine.
3. Extend picture/text/choice variants, database utilities and effects from actual execution. Resolve the recorded delayed-range mismatch. Incompatible development resumes are preserved before a fresh startup.
4. Validate the title flow, new game and a complete gameplay loop against Game.exe; add game-command save/load rather than relying on development snapshots.
5. Validate the expanded CMake cache, measure full/thin build times, and validate signing, installation, updates, input, lifecycle, audio, memory and performance on iPhone.
6. Extend compatibility testing across progression and endings. Old asset-cache versions and renderer texture-cache limits still need cleanup policies.
