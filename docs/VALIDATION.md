# Reproducible compatibility checks

Build and test locally before requesting an iOS build. The original game is never modified. Game resources are staged separately in `build/game`.

## Input replay

The headless runtime accepts `game.json [frames] [common-event-id=-1] [input.json] [state-output.json] [state-input.json]`. Each input row supplies a complete held state starting at its frame. Omitted buttons are released; mouse edges and wheel impulses last one tick. Frame numbers start at zero and must increase strictly. With a state input, `frames` is the number of additional ticks; the replay is aligned to the restored frame.

This recording opens the title menu and selects New Game:

```json
[
  {"frame":60,"confirm":true},
  {"frame":61},
  {"frame":150,"confirm":true},
  {"frame":151}
]
```

Save it as `build/title-newgame-input.json`, then run:

```powershell
build/windows-app/wolf-headless.exe build/game/game.json 550 -1 tests/replays/new-game.json build/opening-state.json
```

The version-controlled [new-game replay](../tests/replays/new-game.json) reaches the opening notice without a compatibility stop. The [title replay](../tests/replays/title-menu.json) idles at the menu. The [opening replay](../tests/replays/opening.json) dismisses the notice, clicks the language selector and advances through opening choices/dialogue:

```powershell
build/windows-app/wolf-headless.exe build/game/game.json 1200 -1 tests/replays/opening.json build/choice-state.json
build/windows-core-release/wolf-headless.exe build/game/game.json 12200 -1 tests/replays/opening-extended.json build/prologue-state.json
```

The 1,200-frame run is a passing interpreter check at a visible choice menu. The extended replay reaches frame 12,200 without a compatibility stop, and its next-frame native capture now renders the home screen. Retained show-picture blend/anchor/transform values are resolved before rendering. State output is written only if execution has not halted. Release builds avoid the debug interpreter's substantial overhead; desktop replay speed does not establish iPhone performance.

To continue a successful saved development state without replaying the title, pass it as the final argument and retain the same absolute-frame input recording. Source/game compatibility is checked before restoring it.

## Renderer capture

The frontend can use the same input recording and capture a frame without interacting with the desktop:

```powershell
$env:SDL_VIDEODRIVER='dummy'
$env:SDL_AUDIODRIVER='dummy'
$env:WOLF_STORAGE_DIR="$PWD/build/capture-userdata"
$env:WOLF_REPLAY_INPUT="$PWD/build/title-input.json"
$env:WOLF_CAPTURE_FRAME='150'
$env:WOLF_CAPTURE_BMP="$PWD/build/native-menu.bmp"
build/windows-app/WolfPort.exe build/game
```

Use a fresh storage directory to replay from startup. `WOLF_CAPTURE_BMP` requires a positive `WOLF_CAPTURE_FRAME`; capture mode advances one simulation tick per render and exits at the target or a compatibility stop. It writes a BMP plus JSON containing the actual picture records and execution diagnostics. This development mode checks decoding and drawing; it does not measure real-time audio synchronization or device performance.

A state produced by the shared headless runtime can also be copied to that fresh storage directory as `resume.json`, then captured at the next frame. This avoids repeatedly interpreting the title when checking a dialogue/choice layout. Current development states use version 6; earlier states are preserved and rejected by the frontend before a fresh startup.

## Reference comparisons and limits

The isolated Windows reference confirmed archive priority, absent numeric database-row reads and relative common calls (`600100` means the current common event). A relative-call fixture produced the expected numeric result 12. Its command list includes the final blank record expected by Game.exe.

Further fixtures verified picture erase/move ranges, retained zoom, absent-picture queries and the empty picture allocated before a delayed show. Draw effects attached before its display are retained. Six text-width fixtures match the original engine exactly, including a tab drawn with its default glyph. The choice-window comparison uses the archived Game.dat and SysDatabase copied into the isolated fixture; using the loose Game.dat instead selects a different font. Reference processes are closed and copied files restored after probing.

Additional fixtures verified arithmetic-range inclusive offsets, sequential operand re-evaluation, preservation of common-self strings, and zero sound frequency selecting the engine's 100% default. A picture-shake model matches 240 frame-marked position samples, including a finite shake returning to its base position. Those cases establish only their tested layouts; they do not establish all effect/audio semantics.

A known mismatch remains for a relative range movement overlapping an already delayed relative movement: reference X=39, native X=29. Other tested immediate/ranged/retained-zoom cases match. Exact fade/flicker/pattern timing, full text hinting, default-position variants and broader audio semantics remain incomplete. See [reference checks](../reports/windows_reference_checks.json).

The opening prompt and title menu have been captured in both implementations. For the Windows menu capture, only the copied title-prompt keyboard poll was replaced by a constant confirm result; the original title/common logic then ran normally. Native menu entry uses the recorded input above. Background variation depends on random state. Text metrics, exact frame ordering, flicker/fade timing and audio looping still need closer comparison.

New Game performs the initial transfers and reaches the home screen. The original Save menu writes a native slot and JPEG preview, and a fresh launch can load that slot back into the home screen. Map rendering, movement/collision, broader gameplay and iPhone gameplay are not verified. Text-picture growth, the renderer texture-cache budget and first-install asset duplication still require device memory checks.

## Original Save and Load menus

The [home-save replay](../tests/replays/home-save.json) extends the opening recording: it opens Save at frame 12,220 and selects slot 2 at 12,520. Run it in the SDL application with writable storage and capture frame 12,780. The current recorded save test resumes a successful development state at 12,501 rather than rendering the entire opening again. The result contains a populated slot 2, a native state file at `saves/save/savedata02.sav.json`, and a 1024×576 preview at `Save/save2.jpg`.

To test Load independently, keep those files, preserve/move `resume.json` out of the storage directory, and use the [title-load replay](../tests/replays/title-load.json) from startup. It opens Continue with the pointer, hovers slot 2, then holds Confirm briefly. A plain mouse click did not confirm this menu in the current test; the on-screen Confirm control supplies the same basic input as the recording. This input issue remains to be resolved.

The successful native load capture at frame 12,650 shows the home screen with Day 1, 18:30 and 3,108 money. Its absolute frame jumps because in-game Load restores the saved simulation clock while keeping it monotonic. This is a tested home/save/restart/load path, not a full gameplay or Windows-save-compatibility claim. [Validation metadata](../reports/opening_validation.json) records the captures and limits without embedding game assets.

Headless tests that need existing slots set `WOLF_STORAGE_DIR` to the app storage's `saves` directory and `WOLF_RESOURCE_STORAGE_DIR` to the app storage directory. The latter resolves generated screenshots separately from native JSON slots. Screenshot commands require the SDL frontend and stop explicitly if left unhandled in a headless run.

## Whole-game implementation work list

Run `python tools/inventory_commands.py build/game/game.json` to regenerate [command_variants.json](../reports/command_variants.json). It groups all 262,338 records into 639 selected layouts across 52 command IDs, preserving sample original locations without copying dialogue or resource contents. Seven IDs currently have no dispatch handler. Existing handlers still contain unsupported variants, so these counts are not a completion percentage. The inventory guides batched command-family work; clean input replays, renderer captures and actual gameplay/save-load comparisons remain separate acceptance checks.

## Home activities

[Training](../tests/replays/home-training.json) resumes the successfully loaded home state at 12,650, clicks Training and Weight Training, advances the dialogue and returns home at 14,451. [Reading](../tests/replays/home-reading.json) resumes the resulting state at 14,450 and returns home at 16,051. Both have passed full SDL replays with every tick rendered and audio enabled through SDL's dummy output. They are development-state continuations, not fresh-start end-to-end or iPhone performance tests. [Activity metadata](../reports/activity_validation.json) records capture hashes and resulting time/AP/Technique values.

The original Windows and native short-window fixtures compare three 64×72 pixel regions exactly, with zero differing pixels for generated heights 28, 48 and 72. The shared window renderer retains border sizes and lets them overlap, matching this supplied skin. Return to Title has a separate regression test covering cleared game state, stopped audio and retained on-disk settings; the original in-game return menu remains untested.


## First iPhone feedback

The [girl interaction replay](../tests/replays/girl-interaction.json) resumes the verified loaded-home state at 12650, clicks the character and reaches the interaction menu at 13250. All 600 ticks have passed through the native SDL renderer. The missing `Debugskip` label is a warning and falls through, matching the supplied Windows executable. The [Communication replay](../tests/replays/girl-communication.json) continues from 13250 through 14150 in the interpreter and exercises the sign-extended BGS stop encoding. See [device-feedback validation](../reports/device_feedback_validation.json) for limits and capture hashes.

Normal launch now ignores development snapshots unless `WOLF_DEV_RESUME=1` or capture mode opts in. Set `WOLF_DEV_RESUME=0` to test normal startup even during a capture. With an existing frame-12650 snapshot, frame 80 renders the original title. The Menu control adds an explicit Return to title action; the iOS safe-area geometry is tested with a 2796x1290 output and bottom/side insets, but actual phone touch behavior still needs confirmation.

The final normal-startup test also includes existing saved settings and native slot 2. The title renders at frame 80; replaying Continue/Load reaches home at 12650. Original and copied slot bytes remain unchanged. Updated iOS source compiled successfully in Actions; build 7 passed archive/digest/ARM64 checks and all four source CI jobs. Phone behavior remains a separate check.


## Second iPhone feedback: transforms and overnight progression

The [bedtime replay](../tests/replays/bedtime.json) and [Bath replay](../tests/replays/bath.json) start at the successful interaction-menu state at frame 13250. Both pass through frame 16250 in SDL with every tick rendered. Bedtime writes an autosave JPEG and advances to Day 2 at 8:30. Its debug output command 106 is handled without a gameplay message. The [morning continuation](../tests/replays/morning-continuation.json) runs another 1800 rendered ticks, to frame 18050. These are limited continuations, not a claim that the full second day or later game is complete.

The isolated Windows/native rotation-and-mirroring fixture matches all 589824 pixels. Both axes remain effective during rotation in the supplied 3.191 executable, despite the current official help's warning about separate-axis scaling. Relative delayed Show uses the live position at activation, confirmed by three reference rectangles. SDL readback checks cover all four flip combinations at zero and 90 degrees. See [metadata](../reports/picture_transform_validation.json) for hashes and limits. Assets and screenshots remain outside Git.

The [morning shop replay](../tests/replays/morning-shop.json) continues from 18050, selects Help at the shop, and passes every native tick through 20250. It completes the shift at Day 2 17:00, with AP 35/60 and money 3358. All four source CI jobs pass for ad7d43e, and the build 8 IPA is verified locally.


## Third feedback: floating controls, choices and audio

The [bath Cancel](../tests/replays/bath-cancel.json) and [interaction Cancel](../tests/replays/night-cancel.json) native replays map basic Cancel to the mouse right-click expected by custom menus. Both finish with no choice active and retain Day 1 18:30. The bath menu's three visible rows correspond to original indices 0, 1 and 3. Empty rows are hidden without changing branch numbering; all-empty choices skip their branches.

The [Day 2 return-home replay](../tests/replays/day2-return-home.json) resumes the prior successful shop state at 20250 and runs 2600 native ticks to 22850, Day 2 18:30. A separate synthetic native fixture executes common 305's original release/preload/play/SE-stop commands using its supplied MP3; the audio backend completes without errors. Core tests also verify channel-selective reserved-SE cancellation and database track metadata.

The floating control toggle is 44 window points, with an 8-point drag threshold. Tests cover tap-versus-drag behavior, safe-area clamping, Cancel edge generation and enlarged aspect-preserving presentation when controls are hidden. The toggle's position and visibility persist in controls.json. Actual phone touch remains a separate validation step. See reports/controls_audio_validation.json.

The [Day 3 overnight replay](../tests/replays/day3-overnight.json) continues from the successful Day 2 interaction-menu development state at 27850. All 3200 native ticks pass through 31050, with an autosave JPEG, Day 3 8:30, AP 60/60 and money 3258. The starting state follows development input attempts; this is a continuation, not a fresh end-to-end run.
# Bedroom feedback verification (2026-11-10)

The earlier Communication continuation checked for errors but did not assert the first submenu. It missed an automatic first selection caused by the mouse adapter's deferred Confirm. New native checks start from build/interaction-state.json at 13250, use tests/replays/bedroom-{communication,interaction,conversation}-menu.json, and render through 13550. Assert common 247 PC 150 with original visible indices [0,2,3], common 246 PC 15 with [0,1,2,3,4,5], and common 247 PC 156 with [0,1,2], respectively. Each choice must remain open, armed and unaccepted. A separate right-click cancellation closes the first submenu while preserving clock/AP variables. The unchanged IR hash is recorded in reports/bedroom_menu_validation.json.

Isolated Windows fixtures confirm that a deferred automatic Confirm immediately before a new choice leaves the choice open; unknown escapes display literally; and six relevant text heights are 14,14,16,10,20,20. Native text dimensions match those heights, including the ATK value with a trailing newline. Pixel-level font identity is not claimed. A native file-read/message fixture renders English/RandomEvent01-05.txt, the source of the reported message error, without changing its contents.

Both local Release builds pass 77 runtime scenarios. SDL tests cover trackpad movement without clicking, tap/release edges, long-press right-click, bounds clamping and direct taps released between ticks. Native mouse-mode startup/cursor presentation passes from home 12650 to 12660. Physical iPhone gestures and full-game progression still require testing.

