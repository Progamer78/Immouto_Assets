"""Download a checksum-verified, workspace-local C++ toolchain. No installer."""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile
import io

ROOT = Path(__file__).resolve().parents[1] / ".tools"


def fetch(url):
    with urllib.request.urlopen(url, timeout=120) as response:
        return response.read()


def archive(url, checksum):
    target = ROOT / url.rsplit("/", 1)[1]
    if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != checksum:
        print("Downloading", target.name, flush=True)
        data = fetch(url)
        if hashlib.sha256(data).hexdigest() != checksum:
            raise RuntimeError(f"Checksum mismatch: {url}")
        target.write_bytes(data)
    print("Extracting", target.name, flush=True)
    with zipfile.ZipFile(target) as source:
        source.extractall(ROOT)


def main():
    ROOT.mkdir(exist_ok=True)
    archive("https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip",
            "554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c")
    version = "3.31.8"
    base = f"https://github.com/Kitware/CMake/releases/download/v{version}/"
    name = f"cmake-{version}-windows-x86_64.zip"
    checks = fetch(base + f"cmake-{version}-SHA-256.txt").decode()
    checksum = next(line.split()[0] for line in checks.splitlines() if line.split()[-1] == name)
    archive(base + name, checksum)
    ninja = fetch("https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip")
    if hashlib.sha256(ninja).hexdigest() != "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a":
        raise RuntimeError("Ninja checksum mismatch")
    with zipfile.ZipFile(io.BytesIO(ninja)) as source:
        source.extractall(ROOT / "ninja")
    print("Toolchain ready in", ROOT, flush=True)


if __name__ == "__main__":
    main()
