"""Safely extract a private game package on a GitHub macOS runner."""
from pathlib import Path, PurePosixPath
import stat
import sys
import zipfile


def extract(archive_path, output):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive_path) as archive:
        for entry in archive.infolist():
            relative = PurePosixPath(entry.filename.replace("\\", "/"))
            if relative.is_absolute() or ".." in relative.parts or ":" in entry.filename:
                raise ValueError("Unsafe archive member")
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise ValueError("Asset package must not contain symlinks")
            target = output.joinpath(*relative.parts).resolve()
            if not target.is_relative_to(output):
                raise ValueError("Asset path escapes output")
        archive.extractall(output)
    if not (output / "game.json").is_file():
        raise ValueError("Package must contain game.json at its root")


if __name__ == "__main__":
    extract(sys.argv[1], sys.argv[2])
