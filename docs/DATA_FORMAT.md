# Game intermediate representation, version 1

`game.json` is UTF-8 JSON with `format: "wolf-ios-ir"` and `version: 1`. It is generated from loose original data; it is not a saved game. No command is discarded because the runtime has not implemented it yet.

| Root property | Contents |
| --- | --- |
| `game` | Original title/subtitle, display settings, fonts, raw numeric setting blocks, static random table. The unused archive key is blanked. |
| `common` | File-format version and every common event, including metadata and commands. |
| `maps` | Filename, size, tileset ID, flattened original three-layer tiles, events and all pages. A map without tile data has an empty tile array. |
| `databases` | `user`, `mutable`, `system`; each preserves type ID method, original field positions, rows and `.project` names/defaults/options. Rows expose fields in original logical field order. |
| `tilesets` | Original image references, 31 autotile references, tags and raw passability words. |

Every command stores `code`, `args` (signed 32-bit integer values), `strings`, `indent` and `offset` (byte offset in its original `.dat`/`.mps` file). An optional `route` stores all six movement-setting bytes and each route command's code, integer arguments and byte arguments. Blank records remain present.

Common events retain IDs, names, run mode, condition operator/variable/value, declared argument counts, argument names/options/defaults, 100 self-variable names, editor metadata and return-value metadata. Map pages retain graphic settings, raw condition bytes, four variables/values, trigger mode, movement route and trailing page metadata.

`reports/game_inventory.json` records source SHA-256 values, file/format totals and complete command frequencies. It deliberately does not include dialogue or archive keys. `reports/event_commands.csv` provides the same command inventory in a smaller form.

The native snapshot format (`wolf-ios-state`, version 1) is separate and currently developmental. It is not compatible with original Windows `.sav` files. Do not treat a tested state serialization primitive as a verified implementation of the game's save/load screens or event commands.
