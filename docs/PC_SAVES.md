# Import a PC save

1. Download `wolf-pc-convert.exe` from the latest private release onto Windows.
2. Drag the game's `SaveDataNN.sav` onto it. It creates a `.pc-save.json`
   beside the original. The original stays untouched. Python is not required.
3. Copy that JSON to **Files → On My iPhone → Monochrome → imports**.
4. Open **Menu → Import PC save**, select the file, then return to the title
   and choose **Load Game**. The import uses a vacant normal slot (2–32).

This transfers persistent progress, global/common/map variables, names and
the mutable game database for the supplied game. It restarts the game's
normal load processing; it does not resume the Windows interpreter inside
an animation or dialogue. Imports show a neutral thumbnail until saved again.
Use the game's regular PC saves rather than a debugger's process snapshot.
Cached Windows text labels are imported as plain text; labels reloaded from
game resources regain their original formatting. Phone display preferences
are kept rather than importing Windows display settings.

Renaming a binary `.sav` to `.json` does not convert it. The converter rejects
other games and the app checks database compatibility before writing a slot.
Existing iPhone saves, the imported file and current play are retained.
Cross-version/mod compatibility still depends on matching game data.

The converter uses Sinflower's MIT-licensed [WolfSave](https://github.com/Sinflower/WolfSave),
revision `537ceba4262368c61bfb9c4fa5333341a115c662`. It runs locally and uploads nothing.
The source and license are under `third_party/wolfsave`.

Build from source on Windows with CMake:

```powershell
cmake -S . -B build-pc -DWOLF_BUILD_APP=OFF -DWOLF_BUILD_PC_CONVERTER=ON
cmake --build build-pc --config Release --target wolf-pc-convert
```
