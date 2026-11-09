"""Read this title's loose WOLF 3.x data without modifying the source files.

Framing follows djytw/wolf-rpg-formats (MIT; see THIRD_PARTY_NOTICES.md).
All unknown command IDs, argument bits and metadata are retained in the IR.
Unsupported binary formats fail with a filename and byte offset.
"""
import struct
from pathlib import Path


class FormatError(ValueError):
    pass


class Reader:
    def __init__(self, data, name="<memory>"):
        self.data, self.name, self.pos = data, str(name), 0

    @classmethod
    def file(cls, path):
        return cls(Path(path).read_bytes(), path)

    def error(self, message):
        raise FormatError(f"{self.name}:0x{self.pos:x}: {message}")

    def take(self, count):
        if count < 0 or count > len(self.data) - self.pos:
            self.error(f"need {count} bytes, have {len(self.data) - self.pos}")
        result = self.data[self.pos:self.pos + count]
        self.pos += count
        return result

    def expect(self, value):
        if self.take(len(value)) != value:
            self.error(f"expected marker {value.hex()}")

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def count(self):
        value = self.u32()
        if value > 2_000_000:
            self.error(f"unreasonable collection count {value}")
        return value

    def string(self):
        raw = self.take(self.count())
        if not raw or raw[-1] != 0:
            self.error("string has no NUL terminator")
        try:
            return raw[:-1].decode("utf-8")
        except UnicodeDecodeError as exc:
            self.error(f"invalid UTF-8: {exc}")

    def strings(self):
        return [self.string() for _ in range(self.count())]

    def ints(self):
        return [self.i32() for _ in range(self.count())]

    def finish(self, footer=None):
        if footer is not None:
            self.expect(bytes([footer]))
        if self.pos != len(self.data):
            self.error(f"{len(self.data) - self.pos} trailing bytes")

    def header(self, suffix):
        self.expect(b"\x00W\x00\x00OL")
        if self.u8() != 0x55:
            self.error("only this game's UTF-8 WOLF 3.x data is supported")
        self.expect(suffix)
        return self.u8()


def route(r):
    flags = list(r.take(6))
    entries = []
    for _ in range(r.count()):
        code = r.u8()
        args = [r.i32() for _ in range(r.u8())]
        extra = list(r.take(r.u8()))
        entries.append({"code": code, "args": args, "extra": extra})
    return {"flags": flags, "commands": entries}


def command(r):
    offset, count = r.pos, r.u8()
    if count == 0:
        return {"code": 0, "args": [], "indent": 0, "strings": [], "offset": offset}
    code = r.u32()
    args = [r.i32() for _ in range(count - 1)]
    indent = r.u8()
    strings = [r.string() for _ in range(r.u8())]
    has_route = r.u8()
    if has_route not in (0, 1):
        r.error(f"invalid move-route flag {has_route}")
    result = {"code": code, "args": args, "indent": indent, "strings": strings, "offset": offset}
    if has_route:
        result["route"] = route(r)
    return result


def common_events(path):
    r = Reader.file(path)
    version = r.header(b"FC\x00")
    events = []
    for _ in range(r.count()):
        r.expect(b"\x8e")
        eid, mode = r.u32(), r.u8()
        variable, value = r.i32(), r.i32()
        nargs, sargs = r.u8(), r.u8()
        title = r.string()
        commands = [command(r) for _ in range(r.count())]
        r.expect(b"\x01\x00\x00\x00\x00")
        memo = r.string()
        r.expect(b"\x8f")
        arg_names = r.strings()
        arg_modes = list(r.take(r.count()))
        arg_options = [r.strings() for _ in range(r.count())]
        arg_values = [r.ints() for _ in range(r.count())]
        defaults = r.ints()
        r.expect(b"\x90")
        color = r.u32()
        self_names = [r.string() for _ in range(100)]
        r.expect(b"\x91\x01\x00\x00\x00\x00\x92")
        return_name, return_id = r.string(), r.i32()
        r.expect(b"\x92")
        events.append({"id": eid, "name": title, "mode": mode & 15,
                       "condition_operator": mode >> 4, "condition_variable": variable,
                       "condition_value": value, "number_args": nargs, "string_args": sargs,
                       "commands": commands, "memo": memo, "argument_names": arg_names,
                       "argument_modes": arg_modes, "argument_options": arg_options,
                       "argument_values": arg_values, "argument_defaults": defaults,
                       "color": color, "self_names": self_names,
                       "return_name": return_name, "return_id": return_id})
    footer = r.u8()
    if footer not in (0x8f, 0x90, 0x91):
        r.error("invalid common-event footer")
    r.finish()
    return {"version": version, "events": events}


def map_data(path):
    r = Reader.file(path)
    r.expect(b"\x00" * 10 + b"WOLFM\x00\x55\x00\x00\x00")
    if r.u32() != 100 or r.u8() != 102:
        r.error("unsupported map format")
    name = r.string()
    tileset, width, height, count = r.u32(), r.u32(), r.u32(), r.count()
    if width * height > 2_000_000:
        r.error("unreasonable map dimensions")
    first = r.u32()
    tiles = [] if first == 0xffffffff else [first] + [r.u32() for _ in range(width * height * 3 - 1)]
    events = []
    for _ in range(count):
        r.expect(b"\x6f\x39\x30\x00\x00")
        eid, title = r.u32(), r.string()
        x, y, pages_count = r.u32(), r.u32(), r.count()
        r.expect(b"\x00" * 4)
        pages = []
        for _ in range(pages_count):
            r.expect(b"\x79")
            chip, filename = r.i32(), r.string()
            graphic = list(r.take(4))
            trigger = r.u8()
            switches = list(r.take(4))
            variables = [r.i32() for _ in range(4)]
            values = [r.i32() for _ in range(4)]
            moves = route(r)
            commands = [command(r) for _ in range(r.count())]
            if r.u32() != 3:
                r.error("unexpected map-page metadata size")
            metadata = list(r.take(3))
            r.expect(b"\x7a")
            pages.append({"chip": chip, "image": filename, "graphic": graphic, "trigger": trigger,
                          "switches": switches, "variables": variables, "values": values,
                          "route": moves, "commands": commands, "metadata": metadata})
        r.expect(b"\x70")
        events.append({"id": eid, "name": title, "x": x, "y": y, "pages": pages})
    r.finish(0x66)
    return {"file": "MapData/" + Path(path).name, "name": name, "tileset": tileset,
            "width": width, "height": height, "tiles": tiles, "events": events}


def game_settings(path):
    r = Reader.file(path)
    r.expect(b"\x00W\x00\x00OL\x00FM\x55")
    flags = list(r.take(r.count()))
    strings = r.strings()
    if len(flags) < 21 or len(strings) < 9:
        r.error("incomplete game settings")
    if r.u32() != len(r.data) - 1:
        r.error("stored game file size does not match")
    unknown = r.u32()
    settings = [r.u16() for _ in range(r.count())]
    if len(settings) < 19:
        r.error("incomplete display settings")
    # The remainder is the editor's static random table, not executable code.
    randoms = r.take(len(r.data) - r.pos - 1)
    footer = r.u8()
    if footer not in (0xc2, 0xc3, 0xc4):
        r.error("unsupported game settings footer")
    r.finish()
    strings[2] = ""  # Archive key is unnecessary for the loose-data runtime.
    return {"title": strings[0], "subtitle": strings[8], "fonts": strings[3:7],
            "tile_size": flags[0], "fps": flags[4], "width": settings[16], "height": settings[17],
            "flags": flags, "strings": strings, "settings": settings, "unknown": unknown,
            "random_table_hex": randoms.hex(), "footer": footer}


def database_project(path):
    r = Reader.file(path)
    result = []
    for _ in range(r.count()):
        title, fields, names = r.string(), r.strings(), r.strings()
        memo = r.string()
        functions = list(r.take(r.count()))
        unknown = r.strings()
        options = [r.strings() for _ in range(r.count())]
        values = [r.ints() for _ in range(r.count())]
        defaults = r.ints()
        result.append({"name": title, "fields": fields, "names": names, "memo": memo,
                       "functions": functions, "unknown": unknown, "options": options,
                       "values": values, "defaults": defaults})
    r.finish()
    return result


def database(path):
    r = Reader.file(path)
    version = r.header(b"FM\x00")
    types = []
    for _ in range(r.count()):
        r.expect(b"\xfe\xff\xff\xff")
        method, positions = r.u32(), [r.u32() for _ in range(r.count())]
        if any(p // 1000 not in (1, 2) for p in positions):
            r.error("unsupported database field block")
        n = sum(p // 1000 == 1 for p in positions)
        s = len(positions) - n
        rows = []
        for _ in range(r.count()):
            numbers = [r.i32() for _ in range(n)]
            strings = [r.string() for _ in range(s)]
            rows.append([numbers[p % 1000] if p // 1000 == 1 else strings[p % 1000] for p in positions])
        types.append({"id_method": method, "positions": positions, "rows": rows})
    footer = r.u8()
    r.finish()
    project = Path(path).with_suffix(".project")
    schema = database_project(project) if project.exists() else []
    if schema and len(schema) != len(types):
        r.error("database and project type counts differ")
    for i, entry in enumerate(schema):
        types[i]["schema"] = entry
    return {"version": version, "footer": footer, "types": types}


def tilesets(path):
    r = Reader.file(path)
    version = r.header(b"FM\x00")
    if version != 210:
        r.error(f"unsupported tileset version {version}")
    result = []
    for _ in range(r.count()):
        title, image = r.string(), r.string()
        auto = [r.string() for _ in range(31)]
        r.expect(b"\xff")
        tags = list(r.take(r.count()))
        r.expect(b"\xff")
        passable = [r.u32() for _ in range(r.count())]
        result.append({"name": title, "image": image, "autotiles": auto, "tags": tags, "passability": passable})
    r.finish(0xcf)
    return result


def load_project(root, basic=None, map_dir=None):
    root = Path(root)
    basic = Path(basic) if basic is not None else root / "Data" / "BasicData"
    map_dir = Path(map_dir) if map_dir is not None else root / "Data" / "MapData"
    return {"format": "wolf-ios-ir", "version": 1,
            "game": game_settings(basic / "Game.dat"),
            "common": common_events(basic / "CommonEvent.dat"),
            "maps": [map_data(p) for p in sorted(map_dir.glob("*.mps"))],
            "databases": {name: database(basic / (file + ".dat")) for name, file in
                          [("user", "DataBase"), ("mutable", "CDataBase"), ("system", "SysDatabase")]},
            "tilesets": tilesets(basic / "TileSetData.dat")}
