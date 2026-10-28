"""Image/quad behaviour adapted from upstream graphics/image tests, plus ownership tests."""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\0" + bytes(pixels[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def read_screenshot(path):
    """Read LÖVE's RGB/RGBA or palette PNGs without an extra test dependency."""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset, compressed, palette = 8, bytearray(), []
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", payload)
            assert ((depth == 8 and color in (2, 6)) or (color == 3 and depth in (1, 2, 4, 8))) and interlace == 0, (depth, color, interlace)
        if kind == b"PLTE":
            palette = [tuple(payload[i:i + 3]) for i in range(0, len(payload), 3)]
        if kind == b"IDAT":
            compressed.extend(payload)
        offset += length + 12
    channels = {2: 3, 3: 1, 6: 4}[color]
    stride = (width * channels * depth + 7) // 8
    bytes_per_pixel = max(1, channels * depth // 8)
    raw = zlib.decompress(compressed)
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            above = previous[i]
            corner = previous[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = left
            elif kind == 2:
                predictor = above
            elif kind == 3:
                predictor = (left + above) // 2
            else:
                assert kind == 4, kind
                p = left + above - corner
                distances = [abs(p - v) for v in (left, above, corner)]
                predictor = (left, above, corner)[distances.index(min(distances))]
            row[i] = (row[i] + predictor) & 255
        rows.append(row)
        previous = row

    def pixel(x, y):
        # The render test window is 256x192 logical pixels; support Retina output.
        px, py = int((x + 0.5) * width / 256), int((y + 0.5) * height / 192)
        if color == 3:
            shift = 8 - depth - ((px * depth) % 8)
            index = (rows[py][px * depth // 8] >> shift) & ((1 << depth) - 1)
            return palette[index]
        return tuple(rows[py][px * channels:px * channels + 3])
    return pixel


def main():
    executable, renderer = str(pathlib.Path(sys.argv[1]).resolve()), sys.argv[2]
    root = pathlib.Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="lovescua-images-") as directory:
        project = pathlib.Path(directory)
        resources = project / "res"
        resources.mkdir()
        (project / "scua.toml").write_text('[package]\nscua-version = "0.38"\n')
        shutil.copy(root / "testing/resources/love.png", resources / "love.png")
        colors = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255)]
        pixels = []
        for y in range(16):
            for x in range(16):
                pixels.extend((0, 0, 0, 0) if (x, y) == (15, 15) else colors[(y // 8) * 2 + x // 8])
        (resources / "colors.png").write_bytes(png(16, 16, pixels))
        (resources / "invalid.png").write_bytes(b"not an image")
        (project / "outside.png").write_bytes((resources / "colors.png").read_bytes())

        def run(name, source, *, frames=2, error=None, expected=None, screenshot=False, mounts=()):
            script = project / "main.scua"
            script.write_text(source)
            command = [executable, "--no-input", "--renderer", renderer, "--verify", "--frames", str(frames),
                       "--fixed-dt", "0.016666666666666666"]
            if screenshot:
                command += ["--screenshot", str(project / "capture.png")]
            for mount in mounts:
                command += ["--mount", str(mount)]
            result = subprocess.run(command + [str(script)], capture_output=True, text=True, timeout=20)
            output = result.stdout + result.stderr
            assert result.returncode == (1 if error else 0), (name, result.returncode, output)
            if error:
                assert error.lower() in output.lower(), (name, output)
            if expected:
                assert expected in output, (name, output)
            print(f"PASS: {name} ({renderer})")
            return output

        run("upstream Image dimensions / Quad viewport / filtering", '''import love
import assets
let image = nil
let quad = nil
fn load()
  image = assets.image("res:love.png")
  assert(love.graphics.image_dimensions(image) == vec2(64, 64))
  love.graphics.set_filter(image, "linear")
  love.graphics.set_filter(image, "nearest")
  quad = love.graphics.new_quad(image, vec2(16, 8), vec2(32, 16))
end
fn draw() love.graphics.draw_quad(quad, vec2(20, 20)) end
fn verify() assert(love.graphics.quad_dimensions(quad) == vec2(32, 16)) end
''', expected="Resources released: 1 images, 1 quads")

        run("aliases, busy provider, double close, stale slot", '''import love
import assets
let image = assets.image("res:love.png")
let alias = image
let quad = love.graphics.new_quad(image, vec2(0, 0), vec2(32, 32))
let replacement = nil
let ticks = 0
fn load()
  try
    love.graphics.release(image)
    assert(false, "provider closed with live quad")
  rescue error
    assert(error.contains("busy"))
  end
end
fn update(dt)
  ticks = ticks + 1
  if ticks == 1 then
    love.graphics.release(quad)
    love.graphics.release(quad)
    assert(love.graphics.image_dimensions(alias) == vec2(64, 64))
  else
    love.graphics.release(image)
    try
      love.graphics.image_dimensions(alias)
      assert(false, "released image alias was usable")
    rescue error
      assert(error.contains("released") or error.contains("stale"))
    end
    replacement = assets.image("res:love.png")
    love.graphics.release(alias)
    assert(love.graphics.image_dimensions(replacement) == vec2(64, 64))
  end
end
fn draw()
  if ticks == 1 then love.graphics.draw(image, vec2(20, 20))
  else
    love.graphics.draw(replacement, vec2(20, 20))
    love.graphics.release(replacement)
  end
end
fn verify() assert(ticks == 2) end
''', expected="Resources released: 2 images, 1 quads")

        output = run("GC retains providers and retires temporary images/quads", '''import love
import assets
fn retained_quad()
  let image = assets.image("res:love.png")
  return love.graphics.new_quad(image, vec2(0, 0), vec2(32, 32))
end
let kept = retained_quad()
fn load()
  for i in 0:24 do
    let unused = retained_quad()
    assert(love.graphics.quad_dimensions(unused) == vec2(32, 32))
  end
end
fn draw() love.graphics.draw_quad(kept, vec2(20, 20)) end
fn verify() assert(love.graphics.quad_dimensions(kept) == vec2(32, 32)) end
''', frames=120, expected="Resources released: 25 images, 25 quads")
        assert "Resources before teardown: 1 images, 1 quads" in output, output

        run("failed publication releases newly acquired texture", '''import love
import assets
let image = assets.image("res:love.png")
let quads = []
fn load()
  for i in 0:4095 do
    quads.push(love.graphics.new_quad(image, vec2(0, 0), vec2(8, 8)))
  end
  let beyond_limit = assets.image("res:love.png")
end
fn draw() end
''', error="limit", expected="Resources released: 1 images, 4095 quads")

        failures = [
            ("missing asset", 'assets.image("res:missing.png")', "could not load image"),
            ("invalid image", 'assets.image("res:invalid.png")', "decode"),
            ("resource traversal", 'assets.image("res:../outside.png")', "could not load image"),
            ("quad range", 'love.graphics.new_quad(image, vec2(1000001, 0), vec2(8, 8))', "supported range"),
            ("zero quad", 'love.graphics.new_quad(image, vec2(0, 0), vec2(0, 8))', "positive size"),
            ("invalid filter", 'love.graphics.set_filter(image, "bad")', "filter must be"),
            ("wrong resource type", 'love.graphics.quad_dimensions(image)', "type"),
            ("empty encoded data", 'love.graphics.new_image(b"")', "encoded image"),
            ("wrong encoded data type", 'love.graphics.new_image(42)', "encoded image bytes"),
        ]
        for name, action, error in failures:
            run(name, 'import love\nimport assets\nlet image = assets.image("res:love.png")\n'
                + f'fn load() {action} end\nfn draw() end\n', error=error,
                expected="Resources released: 1 images, 0 quads")

        run("cleanup after draw fault with a pending batch", '''import love
import assets
let image = assets.image("res:love.png")
let quad = love.graphics.new_quad(image, vec2(0, 0), vec2(32, 32))
fn draw()
  love.graphics.draw_quad(quad, vec2(20, 20))
  assert(false, "intentional failure after draw")
end
''', error="intentional failure", expected="Resources released: 1 images, 1 quads")

        run("resource reads do not grant filesystem writes", '''import fs
fn load()
  assert(fs.write("res:forbidden", b"no") matches Error)
  try
    fs.write("forbidden", b"no")
    assert(false, "host write was allowed")
  rescue error
    assert(error.contains("not granted --allow-fs"))
  end
  try
    assert(fs.read("outside.png") matches Error, "host read was allowed")
  rescue error
    assert(error.contains("not granted --allow-fs"))
  end
end
fn draw() end
fn verify() end
''')
        assert not (resources / "forbidden").exists() and not (project / "forbidden").exists()

        render = '''import love
import assets
let image = nil
let quad = nil
fn load()
  love.window.set_mode(256, 192)
  image = assets.image("res:colors.png")
  love.graphics.set_filter(image, "nearest")
  quad = love.graphics.new_quad(image, vec2(8, 0), vec2(8, 8))
end
fn draw()
  love.graphics.clear(vec4(0, 0, 0, 1))
  love.graphics.draw(image, vec2(184, 96), nil, nil, nil)
  love.graphics.draw(image, vec2(16, 16), 0.0, vec2(4, 4))
  love.graphics.draw(image, vec2(160, 16), 0.0, vec2(-4, 4))
  love.graphics.draw_quad(quad, vec2(16, 112), 0.0, vec2(4, 4))
  love.graphics.draw(image, vec2(144, 128), 1.57079632679, vec2(4, 4), vec2(8, 8))
  love.graphics.set_color(vec4(1, 0.5, 1, 1))
  love.graphics.draw_quad(quad, vec2(208, 16), 0.0, vec2(4, 4))
end
fn verify() assert(love.graphics.image_dimensions(image) == vec2(16, 16)) end
'''
        run("render image and transformed quads", render, frames=1, screenshot=True)
        pixel = read_screenshot(project / "capture.png")
        red, green, blue, yellow = [tuple(c[:3]) for c in colors]
        samples = {(24, 24): red, (64, 24): green, (24, 64): blue, (64, 64): yellow,
                   (104, 24): green, (152, 24): red, (32, 128): green,
                   (128, 112): blue, (160, 112): red, (128, 144): yellow, (160, 144): green,
                   (78, 78): (0, 0, 0), (224, 32): (0, 128, 0)}
        for position, expected in samples.items():
            actual = pixel(*position)
            assert all(abs(a - b) <= 1 for a, b in zip(actual, expected)), (renderer, position, actual, expected)
        print(f"PASS: screenshot pixels (crop, scale, flip, rotation, origin, tint, alpha) ({renderer})")

        # Explicit host mounts are read-only, and packs work with the same SCUA asset API.
        pack = project / "assets.pak"
        with zipfile.ZipFile(pack, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("packed.png", (resources / "colors.png").read_bytes())
        run("image from a mounted resource pack", render.replace("res:colors.png", "res:packed.png"),
            frames=1, mounts=(pack,), expected="Resources released: 1 images, 1 quads")


if __name__ == "__main__":
    main()
