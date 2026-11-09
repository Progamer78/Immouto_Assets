# Native port architecture

This port targets the supplied Living with Sister: Monochrome Fantasy + DLC v2.02 data and the behavior of its Game.exe (file version 3.191.2023.529). The intended result remains a complete, standalone native ARM64 iOS application.

## Decision

Use a focused C++17 interpreter with SDL2 for native platform integration. Develop and test the same interpreter on Windows; generate an Xcode project using CMake and compile it on GitHub's macOS runners. No Windows executable, browser engine, or remote computer is involved in the resulting application.

Existing projects were inspected before this choice:

| Project | Inspected revision | Finding |
| --- | --- | --- |
| take-cheeze/rpg-maker-clone | df3ee7c520085e431c9951bb5be6cfa1e6401c85 | mruby-wolf declares MIT licensing. It has useful parsers/interpreter work, but incomplete picture variants, input and save state. Its shared RGSS/mruby stack also covers unrelated engines. It is a reference rather than the initial application base. |
| djytw/wolf-rpg-formats | 5c70e643693e08f99bf299a2ca598f7eaa02c542 | MIT format descriptions; used to implement complete binary framing for this title. Not a runtime. |
| popupie/wolftools | fad1d573a3abb468f2d9a7510b0e77e9be023c45 | MIT archive/inspection/browser preparation tool. Not a native runtime. |
| G1org1owo/wolfrpg-map-parser | 59dc4578c0b7cd925da47e33b02a5173aa4bb856 | MIT command framing and argument enums; independent reference for numeric operations, input, database and transition configuration. |
| gdlbo/makerplay | c01d80a48200189fcf93f310cfffb12828c923f6 | Additional candidate reviewed during reassessment: Android integration with a Rust/SDL3 WOLF runtime. No reuse license was identified in the inspected repository root or crate manifest, and compatibility with this title/iOS was not tested. Its code has not been copied. |

This choice remains provisional. An existing runtime proven against this game, with appropriate reuse terms, could reduce the work. None of the inspected alternatives has established that result. The current C++/SDL approach meets the platform requirements, but full-game compatibility and the development effort remain unproven. The next useful acceptance gate is a new-game sequence followed by saving, quitting and loading; a compiled IPA or title screen does not satisfy it.

The C++ interpreter executes original commands. Unknown command IDs or unsupported argument layouts stop execution and record the original byte offset and event context. Frame-based input replays and local renderer captures make failures reproducible without repeatedly building an IPA. Focused probes run the supplied Windows executable only in an isolated copy to check behavior that format descriptions leave uncertain.

## Data path

`Original archives/folders -> offline archive resolution -> Python binary reader -> game.json IR -> C++ interpreter -> SDL renderer/input`

`tools/wolf_data.py` uses only the Python standard library. It validates markers, string encoding, bounds and complete consumption of each supported file. Original command IDs, numeric arguments, strings, branch depths and movement routes are retained. Common-event metadata, map event pages, database schemas and tileset passability are also retained.

The intermediate format is documented in [DATA_FORMAT.md](DATA_FORMAT.md). Python and Node are needed during preparation only. The iOS application reads JSON using a vendored C++ library and executes native compiled code.

The supplied `.wolf` archives take priority over loose folders. This was verified by changing a title marker in an isolated copy: with the archive present Game.exe used its original title; without it the edited loose title appeared. The archives contain 600 common events rather than the loose folder's 550. The converter now decodes these archives into a separate hash-verified cache using the vendored MIT wolftools modules. It never writes into the supplied original. See [reference checks](../reports/windows_reference_checks.json).

## Interpreter and state

Each running event has a stack of frames, program counters, loop counters, a wake frame, and event identity. A called common event shares the caller's execution stream so its waits suspend the caller. Parallel streams retain independent wake frames. Execution priority follows the documented map/automatic-common/parallel-common order; trigger re-evaluation, restart/cancellation and blocking rules still need behavioral comparisons. Character property routes retain their execution position and caller wait; positional movement and jumps remain unsupported.

Variable banks distinguish ordinary, system, common-self and per-map event-self state. Database operations resolve explicit name flags; numeric table IDs can be stale when a name is selected. This was observed in real startup commands and independently supported by WOLF's documentation and the Rust parser.

Native development snapshots capture all currently implemented state, including variables, strings, databases, pictures, character properties/routes, stacks, loop counters, wake frames and RNG state. Writes use a flushed temporary file and atomic replacement. Original Windows saves and whole-game SaveLoad are not yet supported. Separate partial variable files support commands 221/222 for settings/unlocks. A halted interpreter cannot create a snapshot that would skip the failed command on resume.

Version 6 development snapshots also retain blocking-message progress, delayed picture changes, delayed SE dispatch, clock counters and input permissions. Immediate hero transfers preserve the blocking caller and remove the previous map's parallel runs. Coordinates retain half-tile units. These scheduling/transfer rules still need broader original-engine comparisons.

Text measurement and drawing share the same native rasterizer. Logical bounds exclude the transparent canvas margin used to draw outlines/shadows. Six width fixtures, including tabs, agree with the supplied engine; full GDI hinting parity is not established. Delayed shows allocate an initially empty picture immediately, so existence queries and effects work before its image/text appears. Picture draw effects remain separate from values returned by picture queries. A delayed-relative/ranged-movement combination is a recorded mismatch.

## Platform and build

SDL2 is pinned to commit 5d249570393f7a37e037abf22cd6012a4cc56a71 (2.32.10). The frontend renders original PNG/JPEG pictures, nine-slice window skins and an initial subset of text controls using the supplied font and stb_truetype. It supports basic picture interpolation, delayed changes/fades, anchors, negative color flicker, separate-axis scaling, built-in choice windows, mouse/touch input and controller bindings. Miniaudio 0.11.23 and stb_vorbis provide native Ogg/MP3/WAV decoding, mixing, music streaming, pitch and fades through SDL's audio device. Filename playback and delayed SE are implemented; other sound command layouts remain unsupported. Map rendering, complete text/audio semantics, richer effects and full iOS lifecycle validation remain required.

Git history contains source and reports. Game assets are prepared separately for a private Release attachment. A manual iOS workflow downloads those assets when requested, copies them into the application bundle and builds an ARM64 device application. Signed export uses a certificate and profile stored in GitHub secrets. The default unsigned build validates compilation without signing credentials. Native CI has passed on GitHub across Windows, Linux and macOS; the first unsigned iOS check passed in 241 seconds. Its downloaded IPA contains a verified ARM64 executable and deliberately excludes game assets. Signed export and device gameplay remain unverified.

The expanded title-runtime iOS check at `40dada5` passed in 252 seconds and produced a verified 901,232-byte unsigned IPA. It precedes the later dialogue implementation. The corrected CMake cache fallback can reuse a toolchain-compatible dependency cache after the CMake source changes; its warm timing is not yet measured.

Large IPAs are published as private Release attachments, not Actions artifacts. Compiled SDL dependencies are cached using the Xcode version and CMake source as a key. An optional development asset cache installs a full manifest-identified data set once into the app's Documents directory, allowing thin runtime-only IPA updates. The final standalone mode reads the full bundled game directly. Asset installation/reuse is covered by native tests; iOS update persistence, copy progress, Files diagnostics and fatal-signal logging require device validation. Old asset-cache versions are currently retained when content changes.

## References

- [Official WOLF command documentation](https://silversecond.com/WolfRPGEditor/Help/)
- [Database name selection](https://silversecond.com/WolfRPGEditor/Help/04ev_db.html)
- [Variable addressing](https://silversecond.com/WolfRPGEditor/Help/06valueget.html)
- [System variables](https://silversecond.com/WolfRPGEditor/Help/06systemvalue.html)
- [SDL iOS integration and lifecycle](https://wiki.libsdl.org/SDL2/README-ios)
- [GitHub Xcode signing](https://docs.github.com/en/actions/how-tos/deploy/deploy-to-third-party-platforms/sign-xcode-applications)
