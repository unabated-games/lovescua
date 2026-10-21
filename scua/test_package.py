"""Verify a moved macOS package and render one short integration scene."""
import hashlib
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    license_file = pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else ROOT.parent / "scua/packaging/LICENSE"
    game = pathlib.Path(sys.argv[3]).resolve() if len(sys.argv)>3 else ROOT / "examples/instancing"
    spec = importlib.util.spec_from_file_location("lovescua_package", ROOT / "scua/package.py")
    packager = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True
    spec.loader.exec_module(packager)
    with tempfile.TemporaryDirectory(prefix="lovescua-relocation-") as directory:
        root = pathlib.Path(directory)
        built = root / "original"
        packager.build_package(executable, game, built, license_file)
        moved = root / "Moved game with spaces"
        shutil.move(built, moved)
        manifest = json.loads((moved / "manifest.json").read_text())
        for name, digest in manifest["files"].items():
            assert hashlib.sha256((moved / name).read_bytes()).hexdigest() == digest, name
        binary = moved / "bin/lovescua"
        modules = pathlib.Path(packager.command(binary, "--print-module-path"))
        assert modules == (moved / "share/lovescua").resolve()
        for target in [binary, *(moved / "lib").iterdir()]:
            dependencies, rpaths = packager.macho_commands(target)
            assert not rpaths
            for dependency in dependencies:
                assert dependency.startswith(packager.SYSTEM_PREFIXES + ("@loader_path/",)), dependency
                if dependency.startswith("@loader_path/"):
                    assert (target.parent / dependency[len("@loader_path/"):]).is_file(), dependency
        unrelated = root / "unrelated working directory"
        unrelated.mkdir()
        result = subprocess.run([str(moved / "run"), "--no-input", "--verify", "--frames", "3", "--fixed-dt", "0.016666666666666666"], cwd=unrelated,
                                env={**os.environ, "DYLD_LIBRARY_PATH": str(root / "absent"), "DYLD_FALLBACK_LIBRARY_PATH": str(root / "absent"), "ALSOFT_DRIVERS": "null"}, capture_output=True, text=True, timeout=30)
        print(result.stdout, end="")
        if result.returncode:
            print(result.stderr, file=sys.stderr)
        assert result.returncode == 0
        assert "PASS:" in result.stdout
        # Packaging validates inputs before it can replace any prior output.
        try:
            packager.build_package(executable, game, moved, license_file)
        except ValueError:
            pass
        else:
            raise AssertionError("packager overwrote an existing output")
        print("PASS: moved package, spaces, unrelated CWD, bundled dependencies/resources, hashes and retained output")


if __name__ == "__main__":
    main()
