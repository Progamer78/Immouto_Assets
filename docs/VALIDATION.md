# Reproducible compatibility checks

Build and test locally before requesting an iOS build. The original game is never modified. Game resources are staged separately in `build/game`.

## Input replay

The headless runtime accepts `game.json [frames] [common-event-id=-1] [input.json] [state-output.json]`. Each input row supplies a complete held state starting at its frame. Omitted buttons are released; mouse edges and wheel impulses last one tick. Frame numbers start at zero and must increase strictly.

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
build/windows-app/wolf-headless.exe build/game/game.json 500 -1 build/title-newgame-input.json
```

The current implementation stops at command 130 in common event 42, offset 104264, rather than skipping the missing map transfer. This is a compatibility failure, not a passing gameplay test. A replay with only the first two rows idles at the title menu without a compatibility stop. State output is written only if execution has not halted.

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

## Reference comparisons and limits

The isolated Windows reference confirmed archive priority, absent numeric database-row reads and relative common calls (`600100` means the current common event). A relative-call fixture produced the expected numeric result 12. Its command list includes the final blank record expected by Game.exe.

The opening prompt and title menu have been captured in both implementations. For the Windows menu capture, only the copied title-prompt keyboard poll was replaced by a constant confirm result; the original title/common logic then ran normally. Native menu entry uses the recorded input above. Background variation depends on random state. Text metrics, exact frame ordering, flicker/fade timing and audio looping still need closer comparison.

New Game currently reaches the first transfer request. Map rendering, movement/collision, dialogue progression, full game save/load, and iPhone gameplay are not verified. Development resumes are not finished game saves, and music playback position is not yet restored with them.
