#!/usr/bin/env python3
"""Fetch pinned public FDS diagnostics; no BIOS or commercial game downloads."""

import argparse
import hashlib
import io
import json
from pathlib import Path
import tempfile
import urllib.request
import zipfile


def fetch(entry, destination):
    target = destination / entry["filename"]
    if target.exists():
        if target.stat().st_size > 65516:
            raise ValueError(f"Diagnostic file is too large: {target}")
        if hashlib.sha256(target.read_bytes()).hexdigest() != entry["sha256"]:
            raise ValueError(f"Refusing to replace a different file: {target}")
        print(f"Verified {target.name}")
        return
    request = urllib.request.Request(entry["url"], headers={"User-Agent": "MapperBus-FDS-tests"})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read(4 * 1024 * 1024 + 1)
    if len(data) > 4 * 1024 * 1024:
        raise ValueError("Diagnostic download exceeds 4 MiB")
    if "member" in entry:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            info = archive.getinfo(entry["member"])
            if info.file_size > 65516:
                raise ValueError("Diagnostic ZIP member exceeds one FDS side")
            # Read the named member into memory; never extract archive paths.
            data = archive.read(info)
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError(f"SHA-256 mismatch for {entry['filename']}")
    destination.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
        temporary.replace(target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(f"Downloaded and verified {target.name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("tmp/fds-diagnostics"))
    args = parser.parse_args()
    manifest = Path(__file__).resolve().parent.parent / "tests/support/fds_diagnostics.json"
    try:
        for entry in json.loads(manifest.read_text())["fixtures"]:
            fetch(entry, args.output)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
