# Game intermediate representation, version 1

`game.json` is UTF-8 JSON with `format: "wolf-ios-ir"` and `version: 1`. It is generated from resolved original data; it is not a saved game. No command is discarded because the runtime has not implemented it yet. Archive folders take priority over loose folders, matching the supplied Game.exe. The current authoritative inventory is 600 common events and 262,338 command records; the loose-only inventory of 550 events was incomplete.

| Root property | Contents |
| --- | --- |
| `game` | Original title/subtitle, display settings, fonts, raw numeric setting blocks, static random table. The unused archive key is blanked. |
| `common` | File-format version and every common event, including metadata and commands. |
| `maps` | Filename, size, tileset ID, flattened original three-layer tiles, events and all pages. A map without tile data has an empty tile array. |
| `databases` | `user`, `mutable`, `system`; each preserves type ID method, original field positions, rows and `.project` names/defaults/options. Rows expose fields in original logical field order. |
| `tilesets` | Original image references, 31 autotile references, tags and raw passability words. |

Every command stores `code`, `args` (signed 32-bit integer values), `strings`, `indent` and `offset` (byte offset in its original `.dat`/`.mps` file). An optional `route` stores all six movement-setting bytes and each route command's code, integer arguments and byte arguments. Blank records remain present.

Common events retain IDs, names, run mode, condition operator/variable/value, declared argument counts, argument names/options/defaults, 100 self-variable names, editor metadata and return-value metadata. Map pages retain graphic settings, raw condition bytes, four variables/values, trigger mode, movement route and trailing page metadata.

`reports/game_inventory.json` records source SHA-256 values, archive provenance, differences from loose files, file/format totals and complete command frequencies. It deliberately does not include dialogue or archive keys. `reports/event_commands.csv` provides the same command inventory in a smaller form. `tools/resolve_data.py` caches decoded folders by archive and decoder hashes and verifies their file hashes before reuse.

The native snapshot format (`wolf-ios-state`, version 2) is separate and currently developmental. It includes character properties, active route positions/waits and waiting callers. It is not compatible with original Windows `.sav` files. Do not treat a tested state serialization primitive as a verified implementation of the game's save/load screens or event commands.

Commands 221/222 use separate native variable files (`wolf-native-variables`, version 1) inside app storage. Names are case-normalized to preserve Windows behavior; a requested `Save/System.sav` becomes `save/system.sav.json`. Numeric and string fields persist across restarts, missing fields/files yield zero/empty values, and source data identity is checked. Writes flush atomically at frame end. Indirect access and reading full game-save state are not implemented yet.
