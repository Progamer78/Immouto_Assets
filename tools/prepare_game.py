"""Inspect, convert and optionally package the supplied game for native builds."""
import argparse
from collections import Counter
import csv
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

from wolf_data import load_project
from resolve_data import resolve_data

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME = ROOT / "Living_With_SIster_Monochrome_Fantasy_Kagura_Decypted" / "Living With SIster Monochrome Fantasy"
COMMAND_NAMES = {
    0: "Blank", 99: "Checkpoint", 101: "Message", 102: "Choices", 103: "Comment",
    104: "Unknown104", 105: "ForceMessageClose", 106: "DebugText", 107: "ClearDebugText",
    111: "NumberCondition", 112: "StringCondition", 121: "SetVariable", 122: "SetString",
    123: "InputKey", 124: "SetVariableEx", 125: "AutomaticInput", 126: "KeyPermission",
    130: "Teleport", 140: "Sound", 150: "Picture", 151: "ScreenColor",
    160: "SetTransition", 161: "PrepareTransition", 162: "ExecuteTransition",
    170: "Loop", 171: "BreakLoop", 172: "BreakEvent", 174: "ReturnToTitle",
    175: "EndGame", 176: "ContinueLoop", 179: "LoopTimes", 180: "Wait",
    201: "MoveRoute", 210: "CallEvent", 211: "ReserveEvent", 212: "Label",
    213: "JumpLabel", 220: "SaveLoad", 221: "LoadVariable", 222: "SaveVariable",
    250: "Database", 270: "Party", 281: "SystemSettings", 290: "Effect",
    300: "CallCommonByName", 401: "Case", 420: "Else", 421: "Cancel",
    498: "LoopEnd", 499: "BranchEnd", 1000: "Screenshot",
}


def copy_changed(source, destination):
    source, destination = Path(source), Path(destination)
    if destination.exists():
        a, b = source.stat(), destination.stat()
        if a.st_size == b.st_size and a.st_mtime_ns == b.st_mtime_ns:
            return str(destination)
    return shutil.copy2(source, destination)


def asset_manifest(output):
    files = []
    for p in sorted(output.rglob("*")):
        if p.is_file() and p.name != "asset-manifest.json":
            files.append({"path": p.relative_to(output).as_posix(), "bytes": p.stat().st_size,
                          "sha256": hashlib.sha256(p.read_bytes()).hexdigest()})
    identity = hashlib.sha256(json.dumps(files, separators=(",", ":")).encode()).hexdigest()
    manifest = {"format": "wolf-ios-assets", "version": 1, "id": identity, "files": files}
    (output / "asset-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    return manifest


def package_assets(output, target):
    output, target = Path(output), Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=1) as archive:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(output))
    temporary.replace(target)


def all_commands(project):
    for event in project["common"]["events"]:
        for cmd in event["commands"]:
            yield f"common:{event['id']}", cmd
    for game_map in project["maps"]:
        for event in game_map["events"]:
            for index, page in enumerate(event["pages"]):
                for cmd in page["commands"]:
                    yield f"{game_map['file']}:{event['id']}:{index}", cmd


def inspect(root, project, sources, provenance):
    common = Counter(c["code"] for e in project["common"]["events"] for c in e["commands"])
    total = Counter(c["code"] for _, c in all_commands(project))
    files = [p for p in root.rglob("*") if p.is_file()]
    binary = [("Data/BasicData/" + name, sources["BasicData"] / name) for name in
              ["Game.dat", "CommonEvent.dat", "DataBase.dat", "CDataBase.dat", "SysDatabase.dat", "TileSetData.dat"]]
    binary += [("Data/MapData/" + p.name, p) for p in sources["MapData"].glob("*.mps")]
    return {
        "game": {key: project["game"][key] for key in ["title", "subtitle", "width", "height", "fps", "tile_size", "fonts"]},
        "common_events": len(project["common"]["events"]),
        "common_command_records": sum(common.values()),
        "all_command_records": sum(total.values()),
        "common_run_modes": dict(Counter(e["mode"] for e in project["common"]["events"])),
        "commands": [{"id": code, "name": COMMAND_NAMES.get(code, "Unknown"),
                      "common_count": common[code], "map_count": total[code] - common[code],
                      "total": total[code]} for code in sorted(total)],
        "maps": [{"file": m["file"], "width": m["width"], "height": m["height"],
                  "events": len(m["events"]), "pages": sum(len(e["pages"]) for e in m["events"])} for m in project["maps"]],
        "databases": {name: [{"id": i, "name": t["schema"]["name"], "rows": len(t["rows"]),
                              "fields": len(t["positions"])} for i, t in enumerate(db["types"])]
                      for name, db in project["databases"].items()},
        "tilesets": len(project["tilesets"]),
        "file_count": len(files), "bytes": sum(p.stat().st_size for p in files),
        "extensions": dict(sorted(Counter(p.suffix.lower() for p in files).items())),
        "source_sha256": {name: hashlib.sha256(p.read_bytes()).hexdigest() for name, p in binary},
        "data_sources": provenance,
        "resource_policy": "Archives take priority over loose folders, verified with the supplied Windows executable in an isolated copy.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--output", type=Path, default=ROOT / "build/game")
    parser.add_argument("--report", type=Path, default=ROOT / "reports/game_inventory.json")
    parser.add_argument("--source-cache", type=Path, help="Separate cache for verified archive extraction")
    parser.add_argument("--stage", action="store_true", help="Copy assets to output/Data; originals are read-only")
    parser.add_argument("--zip", action="store_true", help="Create artifacts/game-assets.zip (implies --stage)")
    args = parser.parse_args()
    root, output = args.game.resolve(), args.output.resolve()
    if not (root / "Data/BasicData/CommonEvent.dat").is_file() and not (root / "Data/BasicData.wolf").is_file():
        parser.error(f"Game data not found: {root}")
    if output == root or root in output.parents or output in root.parents:
        parser.error("Output must be separate from the original game")
    sources, provenance = resolve_data(root, args.source_cache)
    project = load_project(root, basic=sources["BasicData"], map_dir=sources["MapData"])
    report = inspect(root, project, sources, provenance)
    output.mkdir(parents=True, exist_ok=True)
    (output / "game.json").write_text(json.dumps(project, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    csv_path = args.report.with_name("event_commands.csv")
    with csv_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["id", "name", "common_count", "map_count", "total"])
        writer.writeheader()
        writer.writerows(report["commands"])
    if args.stage or args.zip:
        # Keep every Data resource, including dialogue/DLC, with original relative paths.
        # Archives are decoded during preparation; the app consumes resolved data.
        shutil.copytree(root / "Data", output / "Data", dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns("*.wolf"), copy_function=copy_changed)
        for name, source in sources.items():
            shutil.copytree(source, output / "Data" / name, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns(".extraction.json"), copy_function=copy_changed)
        for p in root.iterdir():
            if p.is_file() and (p.suffix.lower() == ".ttf" or p.name == "dlc.dat"):
                copy_changed(p, output / p.name)
        manifest = asset_manifest(output)
        print("Asset identity:", manifest["id"], flush=True)
        print("Assets staged:", output)
    if args.zip:
        target = ROOT / "artifacts/game-assets.zip"
        package_assets(output, target)
        print("Private build asset package:", target)
    print(f"Parsed {report['common_events']} common events, {len(project['maps'])} maps, "
          f"{report['all_command_records']} command records, {len(report['commands'])} command IDs")
    print("Native runtime data:", output / "game.json")
    print("Inventory:", args.report)


if __name__ == "__main__":
    main()
