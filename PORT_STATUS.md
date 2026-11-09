# Native port status — 2026-11-09

**In progress. The full IPA runs on the user's iPhone, with gameplay and interface defects still being fixed. The complete game is unfinished.**

## First iPhone feedback and fixes

- Command 105 now force-closes the active message and unblocks its owning event. The original byte offset 2195772 belongs to this operation.
- Missing `Debugskip` in common 531 now falls through with a diagnostic warning, matching an isolated test of the supplied Windows engine. The actual girl-click replay reaches the interaction menu through frame 13250, including 600 fully rendered native ticks.
- Picture moves preserve their existing anchor, matching three Windows reference cases. This fixes the cause of alternating displaced dialogue/name windows.
- Command 290 group zoom supports the interaction menu's 100/105 percent effects. A Windows 150 percent fixture confirms both position and dimensions scale about the shared center; base picture queries stay unchanged.
- Normal launches use the original title/Continue flow. Developer snapshots require explicit opt-in or capture mode. A rendered startup with an existing gameplay snapshot reaches the original title at frame 80.
- Bottom controls use the iOS safe area and a taller strip. A new Menu button provides a confirmed Return to title action, including recovery from interpreter compatibility stops. Saved slots are retained.
- These fixes pass local Debug/Release runtime and SDL tests. Updated touch behavior and same-app thin-update persistence still require confirmation on the phone.

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

Reassessed existing runtimes on 2026-11-09. No tested replacement currently establishes compatibility with this game and a usable native iOS build. Browser Woditor's inspected distribution has no native source and explicitly describes itself as not open source; its documented browser route conflicts with TASK.md. The interpreter decision remains provisional, not a claim of the fastest possible solution. Development now prioritizes grouped command-family work and a complete gameplay/save-load loop over fine visual comparisons. The [whole-game audit](reports/command_variants.json) inventories 639 selected layouts across all 262,338 command records; it is not a compatibility percentage.

- Strict, complete-consumption readers for Game.dat, CommonEvent.dat, nine `.mps` files, three databases and their `.project` schemas, and TileSetData.dat.
- Original commands, argument bits, strings, pages, routes and database metadata preserved in the [documented IR](docs/DATA_FORMAT.md).
- Variable banks, per-map event-self identity, database addressing, common-event call stacks, frame waits, branches, labels and counted loops have an initial native implementation.
- Character property routes retain speed, appearance/animation settings, route waits and caller waits. Character movement/jumps and their rendering remain unimplemented.
- Command 220 saves/loads currently implemented game state in native JSON slots. Commands 221/222 share their numeric/string variables, case-insensitive filenames, missing-file defaults, existence checks and frame-end atomic writes. Saved database reads support the computed addresses used by this game's slot menu. The original Save menu writes slot 2 and its JPEG preview locally; original Windows saves remain unsupported.
- Development snapshots preserve implemented stacks, loop/wake state, databases, picture records, delayed pictures/SE, text progress, input permissions, game clocks and RNG. Atomic replacement and rejection of incompatible/corrupt state are tested. The current development format is version 6. In-game Load clears running events/reserved work; development resume preserves them. Track metadata and estimated simulation-clock music positions are restored, without a device synchronization claim.
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
| 121 | Basic arithmetic/assignments, random/remainder/AND, literal/indirect operands and destinations, real intermediate calculations, direct inclusive-offset ranges with per-cell operand re-evaluation, and result binding; indirect ranges and advanced operators missing |
| 122 | Literal/direct/indirect string copy and append, basic deletion/replacement, UTF-8 text-file reads, missing-file and basic file-existence queries; other string operations missing |
| 123–126 | Basic/mouse queries, next-frame automatic basic input, basic-button and mouse-device permissions, picture dimensions/existence/reveal/hover queries; additional device/query variants missing |
| 130 | Immediate hero transfers with half-tile coordinates, retained/stopped map audio and cancellation of the old map's parallel runs. Transition modes and other targets remain unsupported |
| 140 | Filename SE/BGM/BGS playback, inline filename volume/pitch/loop metadata, delayed SE, zero-frequency default, native pitch/mixing, streamed music, BGM/BGS stop/fade/loops and estimated saved playback position; other stop/database/preload variants missing |
| 150 | File/text/window pictures, built-in squares/frames/circles/gradients/triangles/lines, separate zoom axes, retained show values, colors/brightening, basic show/move interpolation, delays, inclusive erase/move ranges and delay reset. Free transforms, scroll links and some delayed-range interactions remain incomplete |
| 160 | Transition settings retained; actual transition rendering missing |
| 170–172, 176, 179, 180, 498 | Loops, break/end, counted iterations, waits |
| 175, 210, 212, 213, 300 | Quit, common-event calls, labels/jumps; some call variants missing |
| 201, 202 | Character property routes, waits, route replacement and waiting callers; repeating and positional routes missing |
| 220–222 | Full native save/load of implemented state, partial numeric/string variables, and computed saved-variable/database reads used by slot details. Original Windows saves and broader indirect write variants unsupported |
| 1000 | Native JPEG/PNG screenshots and writable thumbnail deletion; other file operations unsupported |
| 250 | Basic field read/write, explicit names, row/field counts, name-to-ID queries; absent numeric row reads match the verified zero result. Other utilities missing |
| 270 | Initial party image insertion/replacement/removal, erase-all and visibility state; movement/rendering fidelity unverified |
| 290 | Group zoom about a shared screen coordinate, picture draw offsets, color changes including brightening, flicker, finite/repeating shake and initial automatic pattern variants; automatic-zoom resets. Nonzero automatic zoom and other effects remain unsupported |

Every unsupported command/layout stops and records its context and event stack. Execution priority follows the documented map/automatic-common/parallel-common order. Trigger re-evaluation, cancellation, many system defaults and blocking rules still need reference validation.

## Verification and current incompatibility

- Python tests: **14 passing**, including authoritative archived-game structure, archive priority/cache integrity and malformed-data/build-helper checks.
- Native runtime: **66 passing scenarios** in local Debug and Release builds. New cases include native save/load, saved database reads, music resume metadata, inline/optional sound fields, screenshot scheduling and Return to Title resetting live state while retaining saved settings. SDL tests cover JPEG/PNG previews and overlapping short window borders.
- Windows native application and headless runtime compile using a workspace-local Zig/CMake/Ninja toolchain.
- GitHub workflow syntax checked with checksum-verified actionlint 1.7.7.
- First remote [native check run](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585759780) passed: Windows, Linux and macOS core tests, plus macOS SDL frontend compilation. It completed in approximately 65 seconds including runner setup.
- The 47-scenario [native checks](https://github.com/Progamer78/Immouto_Assets/actions/runs/42667430617) passed for checkpoint `a576994` on Windows/Linux/macOS plus macOS frontend compilation. The newer save/load work requires its own source CI check. Supplied-game tests run locally; CI intentionally has no private game assets.
- Relative calls are now verified against Game.exe: `600100` means the current common ID; `600094` from event 270 calls event 264. A valid isolated fixture produced the expected numeric result 12.
- Authoritative startup and recorded title-menu entry both run **300 frames without a compatibility stop**. Native captures show the opening prompt, title art, menu options and font-rendered mod label. A Windows reference menu capture used an injected confirm result in the copied title prompt only. See [startup](reports/startup_trace.json), [menu](reports/menu_trace.json), [reference checks](reports/windows_reference_checks.json) and [reproduction steps](docs/VALIDATION.md).
- New Game reaches its opening notice at **550 frames without a compatibility stop**. A recorded click selects the language menu, reaches the first four-option choice at 1,200 frames, and advances through further dialogue. See [new game](reports/new_game_trace.json), [opening menu](reports/opening_menu_trace.json) and [progression](reports/opening_progression_trace.json).
- The retained picture-blend failure is fixed. Native captures render the home screen at **frame 12,201**, the Save menu at **12,501**, and populated slot 2 at **12,780**, with no compatibility stop in those captures. The native slot and JPEG preview are written to disk. Map rendering, movement/collision, broader gameplay and iPhone gameplay remain unverified.
- A fresh-launch Continue replay reads the slot and loads back into the home screen at **frame 12,650**, retaining Day 1, 18:30 and 3,108 money. The recording uses pointer hover plus Confirm for slot selection; a plain click did not confirm the title's Load menu and remains an input issue. This does not establish a full activity/combat loop or complete save fidelity for unimplemented systems.
- Full SDL replays now complete Weight Training (**12,650–14,451**) and Reading (**14,450–16,051**), rendering every tick and returning to the home screen with changed time/AP/stats. These resume a successful development state; combat, later days and endings remain unverified. See [activity validation](reports/activity_validation.json).
- Three short-window fixtures match the supplied Windows engine pixel for pixel: 64 pixels wide, heights 28/48/72, using the game's 3×72 nameplate skin. Fixed-size corner pieces overlap when necessary rather than stopping the renderer.
- All four [native jobs](https://github.com/Progamer78/Immouto_Assets/actions/runs/42680248726) pass for `e8211df`: 60 core scenarios on Windows/Linux/macOS plus macOS SDL tests. The subsequent window/Return-to-Title checkpoint requires its own CI run.
- Original-engine fixtures verified inclusive/reversed erase ranges, basic ranged movement, retained zoom, missing-picture query defaults and effects attached before a delayed show. Six text-width cases match exactly after separating rasterizer padding from logical bounds and matching per-glyph integer advances. Font hinting and broader timing fidelity remain unverified.
- Further fixtures verified arithmetic-range inclusive offsets and sequential alias behavior, numeric common-self ranges preserving strings, and frequency zero selecting 100%. A picture-shake model matches 240 frame-marked Windows-reference position samples across repeating and finite shakes. This does not establish all picture-effect variants.
- A known mismatch remains when a relative range movement overlaps an already delayed relative movement (reference X=39, native X=29 in the fixture). See [reference checks](reports/windows_reference_checks.json). Sys30 currently reports simulation-tick milliseconds, without the original engine's within-frame wall-time advance.

## iOS and fast build cycle

- Manual GitHub macOS workflow generates an ARM64 iOS 15+ Xcode project; unsigned packaging and optional Apple-profile-based signed export are configured.
- Source checks run without downloading the game. iOS dependencies have a toolchain-keyed Actions cache.
- Source is tracked separately from the corrected **990,994,485-byte** private game ZIP. ZIP CRCs, archived game.json hash and its 600-event inventory were verified. Large packages/IPAs use private Release attachments, not Git history or Actions artifacts.
- Full development install: `include_game=true`, `reuse_assets=true`. Later small updates: `include_game=false`, `reuse_assets=true`, preserving the existing app/data and identifier.
- Standalone bundled mode: `include_game=true`, `reuse_assets=false`.
- Source checkpoints are uploaded to the private [Progamer78/Immouto_Assets repository](https://github.com/Progamer78/Immouto_Assets), branch `master`. After explicit user approval, the corrected asset ZIP was uploaded to the private `game-assets` release and its remote digest verified. No signing secrets are configured or embedded.
- First unsigned, source-only [iOS run](https://github.com/Progamer78/Immouto_Assets/actions/runs/42585937560) passed in **241 seconds overall** (236 seconds for the job). Xcode/SDL configuration took 213 seconds; actual application compilation took 10 seconds. This is not a full-game or signed-build timing.
- Produced and downloaded `Monochrome-unsigned.ipa`: **555,314 bytes**, ARM64 Mach-O executable, bundle ID `com.local.monochromeport`, minimum iOS 15.0. ZIP integrity and bundle/executable metadata verified; no game assets included and no device installation tested. See [reports/ios_build_check.json](reports/ios_build_check.json).
- The private [release](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-1-1) contains this compilation-check IPA. Dependency caching worked; CMake-result caching was added after this baseline and needs a subsequent run to measure its effect.
- Opening-runtime [iOS check](https://github.com/Progamer78/Immouto_Assets/actions/runs/42634139411) passed for `59133fc` in **48 seconds** overall (6 configuration, 17 compilation). The downloaded [957,736-byte IPA](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-3-1) passed ZIP/digest and ARM64/bundle metadata checks. It is unsigned, contains no game assets and has not been installed on an iPhone. It predates the later 47-scenario follow-up.
- Home/save/load [iOS check](https://github.com/Progamer78/Immouto_Assets/actions/runs/42678866407) passed for `c6bbbcf` in **44 seconds** overall (5 configuration, 18 compilation). The downloaded [995,640-byte IPA](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-4-1) passed ZIP/digest and ARM64/bundle metadata checks. It is unsigned, contains no game assets and has not been installed on an iPhone. The subsequent SE/audio-resume fixes require a later iOS compilation check.
- All four [native checks](https://github.com/Progamer78/Immouto_Assets/actions/runs/42683425270) pass for `a399fb9`, including 61 core scenarios and SDL screenshot/window tests. The [full iOS build](https://github.com/Progamer78/Immouto_Assets/actions/runs/42683523069) also passed in **165 seconds overall**, producing a [1,005,677,313-byte unsigned IPA with game assets](https://github.com/Progamer78/Immouto_Assets/releases/tag/ios-build-5-1). Installation/signing instructions are in [IPHONE_INSTALL.md](docs/IPHONE_INSTALL.md); the user confirmed iPhone installation and gameplay, and reported the issues listed below.
- The corrected `v2` fallback cache is now verified by that warm build. Full-asset and signed-build timings remain unmeasured.

## Next work

1. Use the full-game layout work list to implement related command variants together; resolve the visible picture-blend failure and prioritize an actual gameplay/save-load loop. Add map rendering and movement/collision where the supplied content requires them.
2. Validate common-event ID queries, remaining defaults, scheduling dependencies, menu/touch interaction and rendering timing against the Windows engine.
3. Extend picture/text/choice variants, database utilities and effects in families, checking progression branches locally. Retain the recorded delayed-range mismatch for later fidelity validation. Incompatible development resumes are preserved before a fresh startup.
4. Extend the verified home/save/restart/load path into activities/combat and validate state and behavior against Game.exe. Resolve plain-click confirmation in the title's Load menu.
5. Measure full-asset build times and validate signing, installation, updates, input, lifecycle, audio, memory and performance on iPhone.
6. Extend compatibility testing across progression and endings. Old asset-cache versions and renderer texture-cache limits still need cleanup policies.
