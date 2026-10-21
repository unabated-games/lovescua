#!/usr/bin/env python3
"""Build a relocatable macOS game folder from the actual lovescua executable."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")


def command(*args):
    return subprocess.run([str(a) for a in args], check=True, capture_output=True, text=True).stdout.strip()


def macho_commands(binary):
    """Read load commands rather than treating a dylib's own ID as a dependency."""
    dependencies, rpaths = [], []
    current = None
    for line in command("otool", "-l", binary).splitlines():
        fields = line.strip().split()
        if len(fields) == 2 and fields[0] == "cmd":
            current = fields[1]
        elif current in {"LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB", "LC_LOAD_UPWARD_DYLIB"} and line.strip().startswith("name "):
            dependencies.append(line.strip()[5:].rsplit(" (offset ", 1)[0])
        elif current == "LC_RPATH" and line.strip().startswith("path "):
            rpaths.append(line.strip()[5:].rsplit(" (offset ", 1)[0])
    return dependencies, rpaths


def expand_path(name, loader, executable):
    return pathlib.Path(name.replace("@loader_path", str(loader.parent)).replace("@executable_path", str(executable.parent)))


def library_graph(executable):
    graph, inherited = {}, []
    queue = [(executable, inherited)]
    while queue:
        binary, search = queue.pop()
        binary = binary.resolve()
        if binary in graph:
            continue
        dependencies, rpaths = macho_commands(binary)
        search = [expand_path(p, binary, executable) for p in rpaths] + search
        edges = []
        for name in dependencies:
            if name.startswith(SYSTEM_PREFIXES):
                continue
            if ".framework/" in name:
                raise ValueError(f"External frameworks need a framework bundle layout: {name}")
            if name.startswith("@rpath/"):
                candidates = [p / name[len("@rpath/"):] for p in search]
            else:
                candidates = [expand_path(name, binary, executable)]
            resolved = next((p.resolve() for p in candidates if p.is_file()), None)
            if resolved is None:
                raise ValueError(f"Cannot resolve {name} loaded by {binary}")
            edges.append((name, resolved))
            queue.append((resolved, search))
        graph[binary] = edges
    names = {}
    for original in graph:
        if original == executable:
            continue
        if original.name in names and names[original.name] != original:
            raise ValueError(f"Conflicting library filenames: {names[original.name]} and {original}")
        names[original.name] = original
    return graph


def copy_game(game, destination):
    if not (game / "main.scua").is_file():
        raise ValueError("Game directory must contain main.scua")
    selected = set(game.rglob("*.scua"))
    for name in ("scua.toml", "scua-deps.lock"):
        if (game / name).is_file():
            selected.add(game / name)
    for folder in ("res", "deps"):
        source = game / folder
        if source.exists():
            if source.is_symlink():
                raise ValueError(f"Game inputs must not be symlinks: {source}")
            (destination / folder).mkdir(parents=True, exist_ok=True)
            for item in source.rglob("*"):
                if item.is_symlink():
                    raise ValueError(f"Game inputs must not be symlinks: {item}")
                if item.is_file():
                    selected.add(item)
    for source in sorted(selected):
        if source.is_symlink() or not source.resolve().is_relative_to(game):
            raise ValueError(f"Game inputs must remain inside the game directory: {source}")
        relative = source.relative_to(game)
        if any(part in {".git", "__pycache__"} for part in relative.parts):
            continue
        dest = destination / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)


def copy_licenses(destination, graph, scua_license):
    destination.mkdir()
    shutil.copy2(ROOT / "license.txt", destination / "LOVE.txt")
    shutil.copy2(scua_license, destination / "SCUA.txt")
    # Retain source-level notices for the engine's embedded libraries.
    notices, seen = [], set()
    for source in sorted((ROOT / "src/libraries").rglob("*")):
        if not source.is_file() or source.suffix not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        text = source.read_text(errors="replace")[:32768].lstrip("\ufeff\n\r ")
        header = text.split("*/", 1)[0] + "*/" if text.startswith("/*") else "\n".join(line for line in text.splitlines()[:100] if line.startswith("//"))
        if not re.search(r"copyright|license|redistribution|permission", header, re.I) or header in seen:
            continue
        seen.add(header)
        notices.append(f"\n--- {source.relative_to(ROOT)} ---\n{header}\n")
    (destination / "embedded-source-notices.txt").write_text("".join(notices))
    # Include standalone embedded-library notices, not just source headers.
    roots = [ROOT / "src/libraries"]
    for vendor in (ROOT / "build/vendor").glob("*"):
        if vendor.is_dir():
            roots.append(vendor)
    for base in roots:
        for source in base.rglob("*"):
            if source.is_file() and re.match(r"^(copying|licen[sc]e|notice|copyright)([.\-_]|$)", source.name, re.I):
                dest = destination / "embedded" / base.name / source.relative_to(base)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
    for library in graph:
        prefix = next((p for p in library.parents if (p / "INSTALL_RECEIPT.json").is_file()), None)
        if prefix is None:
            continue
        package = prefix.parent.name + "-" + prefix.name
        for source in prefix.rglob("*"):
            if source.is_file() and re.match(r"^(copying|licen[sc]e|notice|copyright)([.\-_]|$)", source.name, re.I):
                dest = destination / package / source.relative_to(prefix)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)


def build_package(executable, game, output, scua_license):
    if sys.platform != "darwin":
        raise ValueError("This packaging workflow currently targets macOS")
    executable, game, output = executable.resolve(), game.resolve(), output.absolute()
    if output.exists():
        raise ValueError(f"Output already exists: {output}; choose a new directory")
    if output.is_relative_to(game):
        raise ValueError("Package output must be outside the game input directory")
    if not scua_license.is_file():
        raise ValueError("Provide the matching SCUA runtime license with --scua-license")
    modules = pathlib.Path(command(executable, "--print-module-path"))
    for name in ("love.scua", "assets.scua", "filesystem.scua"):
        if not (modules / name).is_file():
            raise ValueError(f"Runtime module is missing: {modules / name}")
    graph = library_graph(executable)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lovescua-package-", dir=output.parent) as temporary:
        staging = pathlib.Path(temporary) / "package"
        (staging / "bin").mkdir(parents=True)
        (staging / "lib").mkdir()
        targets = {executable: staging / "bin/lovescua"}
        targets.update({p: staging / "lib" / p.name for p in graph if p != executable})
        for original, target in targets.items():
            shutil.copy2(original, target)
            target.chmod(target.stat().st_mode | 0o200)
        for original, target in targets.items():
            edits = []
            if original != executable:
                edits += ["-id", "@loader_path/" + target.name]
            for name, dependency in graph[original]:
                replacement = ("@loader_path/../lib/" if original == executable else "@loader_path/") + targets[dependency].name
                edits += ["-change", name, replacement]
            _, rpaths = macho_commands(original)
            for rpath in dict.fromkeys(rpaths):
                edits += ["-delete_rpath", rpath]
            if edits:
                command("install_name_tool", *edits, target)
            command("codesign", "--force", "--sign", "-", "--timestamp=none", target)
        shutil.copytree(modules, staging / "share/lovescua")
        copy_game(game, staging / "game")
        copy_licenses(staging / "licenses", graph, scua_license)
        launcher = staging / "run"
        launcher.write_text('#!/bin/sh\nset -eu\nbundle_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec "$bundle_dir/bin/lovescua" "$@" "$bundle_dir/game"\n')
        launcher.chmod(0o755)
        (staging / "RUNNING.txt").write_text("Run ./run from a terminal, or bin/lovescua with a different game directory.\nThis folder includes its SCUA facades and non-system dynamic libraries.\nKeep bin, lib, share, game and licenses together when moving it.\nSave folders remain opt-in through --user-id or --save-dir.\nThis is a local ad-hoc signed macOS build; CPU architectures and binary dependencies are recorded in manifest.json.\n")
        manifest = {"format": 1, "runtime": command(executable, "--version"), "architectures": command("lipo", "-archs", executable).split(), "files": {}}
        for target in sorted(staging.rglob("*")):
            if target.is_file():
                manifest["files"][str(target.relative_to(staging))] = hashlib.sha256(target.read_bytes()).hexdigest()
        (staging / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        # Reject remaining dependencies outside this package before publishing it.
        for target in targets.values():
            dependencies, rpaths = macho_commands(target)
            if rpaths or any(not d.startswith(SYSTEM_PREFIXES + ("@loader_path/",)) for d in dependencies):
                raise ValueError(f"Non-relocatable load commands remain in {target}")
            command("codesign", "--verify", target)
        staging.rename(output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--executable", type=pathlib.Path, default=ROOT / "build/lovescua")
    parser.add_argument("--scua-license", type=pathlib.Path, default=ROOT.parent / "scua/packaging/LICENSE")
    args = parser.parse_args()
    try:
        output = build_package(args.executable, args.game, args.output, args.scua_license)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stderr, file=sys.stderr)
        return 1
    print(f"Packaged game: {output}\nRun: {output / 'run'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
