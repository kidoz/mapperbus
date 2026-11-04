#!/usr/bin/env python3
"""Build a local, ad-hoc-signed app bundle with its non-system dylibs."""

import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile


def output(*command):
    return subprocess.check_output(command, text=True)


def run(*command):
    subprocess.run(command, check=True)


def rpaths(binary):
    lines = output("otool", "-l", str(binary)).splitlines()
    return [lines[index + 2].strip().split(" (offset", 1)[0].removeprefix("path ")
            for index, line in enumerate(lines) if line.strip() == "cmd LC_RPATH"]


def expand(path, loader, executable):
    return Path(path.replace("@loader_path", str(loader.parent))
                .replace("@executable_path", str(executable.parent)))


def dependencies(binary):
    return [line.strip().split(" (compatibility", 1)[0]
            for line in output("otool", "-L", str(binary)).splitlines()[1:]]


def package(executable, icon, plist, destination):
    if destination.suffix != ".app" or destination.is_symlink():
        raise ValueError("Output must be an app directory, not a symlink.")
    with plist.open("rb") as stream:
        metadata = plistlib.load(stream)
    if destination.exists():
        with (destination / "Contents/Info.plist").open("rb") as stream:
            previous = plistlib.load(stream)
        if previous.get("CFBundleIdentifier") != metadata["CFBundleIdentifier"]:
            raise ValueError("Refusing to replace a different application.")

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        bundle = Path(temporary) / destination.name
        contents = bundle / "Contents"
        libraries = contents / "Frameworks"
        libraries.mkdir(parents=True)
        (contents / "MacOS").mkdir()
        (contents / "Resources").mkdir()
        shutil.copy2(plist, contents / "Info.plist")
        shutil.copy2(icon, contents / "Resources/MapperBus.icns")
        main = contents / "MacOS" / metadata["CFBundleExecutable"]
        shutil.copy2(executable, main)
        copied = {}

        def visit(original, target, inherited):
            search = [expand(path, original, executable) for path in rpaths(original)] + inherited
            for name in dependencies(original):
                if name.startswith(("/System/Library/", "/usr/lib/")):
                    continue
                if name.startswith("@rpath/"):
                    candidates = [directory / name.removeprefix("@rpath/") for directory in search]
                else:
                    candidates = [expand(name, original, executable)]
                resolved = next((path.resolve() for path in candidates if path.is_file()), None)
                if resolved is None:
                    raise ValueError(f"Cannot resolve {name} used by {original}")
                # otool -L also prints a dylib's own install name.
                if resolved == original:
                    continue
                if ".framework/" in str(resolved):
                    raise ValueError(f"Non-system framework packaging is not supported: {resolved}")
                library = libraries / resolved.name
                if resolved.name in copied and copied[resolved.name] != resolved:
                    raise ValueError(f"Two libraries share the name {resolved.name}")
                if resolved.name not in copied:
                    copied[resolved.name] = resolved
                    shutil.copy2(resolved, library)
                    library.chmod(library.stat().st_mode | 0o200)
                    visit(resolved, library, search)
                    run("install_name_tool", "-id", f"@rpath/{library.name}", str(library))
                run("install_name_tool", "-change", name,
                    f"@executable_path/../Frameworks/{library.name}", str(target))

        visit(executable, main, [])
        for library in sorted(libraries.iterdir()):
            run("codesign", "--force", "--sign", "-", str(library))
        run("codesign", "--force", "--sign", "-", str(bundle))
        run("codesign", "--verify", "--deep", "--strict", str(bundle))
        if destination.exists():
            shutil.rmtree(destination)
        shutil.move(str(bundle), destination)
    print(f"Created {destination}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--icon", type=Path, required=True)
    parser.add_argument("--plist", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("macOS packaging requires macOS with Xcode command-line tools.")
    package(args.executable.resolve(), args.icon.resolve(), args.plist.resolve(),
            args.output.absolute())


if __name__ == "__main__":
    main()
