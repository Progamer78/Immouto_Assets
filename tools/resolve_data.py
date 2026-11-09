"""Select this game's archived data, matching verified Windows-engine priority."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def node_executable():
    if value := os.environ.get("WOLF_NODE"):
        return value
    local = ROOT / ".tools/node-v22.16.0-win-x64/node.exe"
    if local.is_file():
        return str(local)
    if value := shutil.which("node"):
        return value
    raise RuntimeError("Node.js 22+ is needed to extract this game's archives. Run python tools/bootstrap_windows.py or set WOLF_NODE.")


def resolve_data(game, cache=None):
    game = Path(game).resolve()
    cache = Path(cache or ROOT / "build/source-data").resolve()
    if cache == game or game in cache.parents:
        raise ValueError("Archive extraction cache must be outside the original game")
    result, provenance = {}, {}
    decoder = ROOT / "tools/unpack_archive.mjs"
    modules = sorted((ROOT / "tools/vendor/wolftools").rglob("*.mjs"))
    decoder_id = hashlib.sha256(decoder.read_bytes() + b"".join(p.read_bytes() for p in modules)).hexdigest()
    for name in ("BasicData", "MapData"):
        archive = game / "Data" / (name + ".wolf")
        loose = game / "Data" / name
        if not archive.is_file():
            result[name] = loose
            provenance[name] = {"source": f"Data/{name}", "kind": "loose"}
            continue
        identity = digest(archive)
        target = cache / (identity + "-" + decoder_id[:12])
        marker = target / ".extraction.json"
        valid = False
        if marker.is_file():
            info = json.loads(marker.read_text(encoding="utf-8"))
            valid = bool(info.get("files")) and info.get("archive_sha256") == identity and info.get("decoder_sha256") == decoder_id
            if valid:
                for file in info.get("files", []):
                    path = target / file["path"]
                    if path.resolve().parent != target and target not in path.resolve().parents:
                        raise RuntimeError("Invalid extraction-cache path")
                    if not path.is_file() or path.is_symlink() or digest(path) != file["sha256"]:
                        valid = False
                        break
        if not valid:
            if target.exists():
                raise RuntimeError(f"Extraction cache is incomplete or changed: {target}. Choose a fresh --source-cache directory.")
            cache.mkdir(parents=True, exist_ok=True)
            temp = Path(tempfile.mkdtemp(prefix="extract-", dir=cache))
            try:
                subprocess.run([node_executable(), str(decoder), str(archive), str(temp)],
                               check=True, capture_output=True, text=True, encoding="utf-8", timeout=120)
                files = [{"path": p.relative_to(temp).as_posix(), "sha256": digest(p)}
                         for p in sorted(temp.rglob("*")) if p.is_file()]
                if not files:
                    raise RuntimeError(f"Empty game archive: {archive.name}")
                info = {"archive_sha256": identity, "decoder_sha256": decoder_id, "files": files}
                (temp / marker.name).write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
                temp.rename(target)
            finally:
                if temp.exists():
                    assert temp.resolve().parent == cache
                    shutil.rmtree(temp)
        result[name] = target
        provenance[name] = {"source": f"Data/{archive.name}", "kind": "archive", "sha256": identity,
                            "files": len(info["files"]), "different_from_loose": [f["path"] for f in info["files"]
                            if not (loose / f["path"]).is_file() or digest(loose / f["path"]) != f["sha256"]]}
    return result, provenance
