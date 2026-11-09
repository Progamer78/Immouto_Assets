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
build/windows-app/wolf-headless.exe build/game/game.json 4000 -1 tests/replays/opening.json
```

The 1,200-frame run is a passing interpreter check at a visible choice menu. The extended run currently stops at frame 3161, command 140, offset 46566, requesting a music stop/fade. This is a compatibility failure, not a passing gameplay test. State output is written only if execution has not halted. Release builds avoid the debug interpreter's substantial overhead; desktop replay speed does not establish iPhone performance.

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

A known mismatch remains for a relative range movement overlapping an already delayed relative movement: reference X=39, native X=29. Other tested immediate/ranged/retained-zoom cases match. Exact fade/flicker/pattern timing, full text hinting, default-position variants and broader audio semantics remain incomplete. See [reference checks](../reports/windows_reference_checks.json).

The opening prompt and title menu have been captured in both implementations. For the Windows menu capture, only the copied title-prompt keyboard poll was replaced by a constant confirm result; the original title/common logic then ran normally. Native menu entry uses the recorded input above. Background variation depends on random state. Text metrics, exact frame ordering, flicker/fade timing and audio looping still need closer comparison.

New Game performs the initial transfers and reaches opening dialogue/choices. Map rendering, movement/collision, full game save/load and iPhone gameplay are not verified. Development resumes are not finished game saves, and music playback position is not yet restored with them. Text-picture growth, the renderer texture-cache budget and first-install asset duplication still require device memory checks.
