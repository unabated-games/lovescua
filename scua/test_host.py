"""Exercise the real executable and C++/SCUA boundary, including failure paths."""
import pathlib
import subprocess
import sys
import tempfile


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    cases = [
        ("draw only", "fn draw() end\n", None),
        ("missing draw", "fn update(dt) end\n", "game must define fn draw()"),
        ("syntax error", "fn draw( end\n", "line 1"),
        ("script fault", "fn draw() assert(false) end\n", "draw: line 1"),
        ("native mode", 'import love\nfn draw() love.graphics.circle("invalid", vec2(1, 2), 3) end\n', "mode must be 'fill' or 'line'"),
        ("native vector", 'import love\nfn draw() love.graphics.set_color(vec2(1, 2)) end\n', "must be vec4"),
        ("native number", 'import love\nfn load() love.window.set_mode("wide", 400) end\nfn draw() end\n', "must be a number"),
        ("draw phase", 'import love\nfn load() love.graphics.circle("fill", vec2(1, 2), 3) end\nfn draw() end\n', "inside draw()"),
        ("budget", 'fn update(dt) while true do end end\nfn draw() end\n', "budget"),
        ("capabilities", 'import fs\nfn draw() end\n', "fs"),
        ("input queries", '''import love
fn load()
  let size = love.graphics.dimensions()
  assert(size.x == 960 and size.y == 640)
  let mouse = love.mouse.position()
  assert(mouse.x == mouse.x and mouse.y == mouse.y)
  let held = love.keyboard.is_down("Space")
  assert(held == true or held == false)
end
fn draw() end
''', None),
    ]
    with tempfile.TemporaryDirectory(prefix="lovescua-test-") as directory:
        for name, source, error in cases:
            script = pathlib.Path(directory) / "main.scua"
            script.write_text(source)
            result = subprocess.run([executable, "--no-input", "--frames", "2", str(script)],
                                    capture_output=True, text=True, timeout=15)
            output = result.stdout + result.stderr
            if error is None:
                assert result.returncode == 0 and "Rendered 2 frames" in output, (name, output)
            else:
                assert result.returncode == 1 and error.lower() in output.lower(), (name, result.returncode, output)
            print(f"PASS: {name}")
        declarations = pathlib.Path(directory) / "decls"
        subprocess.run([executable, "--write-decls", str(declarations)], check=True, timeout=15)
        checked_in = pathlib.Path(__file__).parent / "decls"
        generated = {p.name for p in declarations.iterdir()}
        expected = {p.name for p in checked_in.iterdir()}
        assert generated in (expected, expected - {"love_audio.scua", "love_sound.scua"})
        for declaration in declarations.iterdir():
            assert declaration.read_bytes() == (checked_in / declaration.name).read_bytes(), declaration.name
        print("PASS: declarations match the native registry")


if __name__ == "__main__":
    main()
