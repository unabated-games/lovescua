"""Rebuild the original 4-direction, 4-frame robot sheet. Uses only Python's stdlib."""
import pathlib
import struct
import zlib

SIZE = 32
OUTLINE = (15, 31, 48, 255)
DARK = (33, 68, 86, 255)
TEAL = (51, 207, 178, 255)
LIGHT = (174, 250, 222, 255)
ORANGE = (251, 171, 81, 255)
VISOR = (14, 44, 66, 255)


def frame(direction, phase):
    pixels = [[(0, 0, 0, 0) for _ in range(SIZE)] for _ in range(SIZE)]
    bob = [0, -1, 0, -1][phase]

    def rect(x0, y0, x1, y1, color):
        for y in range(max(0, y0), min(SIZE, y1)):
            for x in range(max(0, x0), min(SIZE, x1)):
                pixels[y][x] = color

    rect(7, 28, 25, 30, (0, 0, 0, 45))
    # Boots and articulated arms give the four frames a visible walk cycle.
    left, right = [(0, 0), (-2, 1), (0, 0), (1, -2)][phase]
    for x, offset in [(10, left), (18, right)]:
        rect(x, 23 + offset, x + 5, 28 + offset, OUTLINE)
        rect(x + 1, 24 + offset, x + 5, 27 + offset, ORANGE)
    rect(5, 16 + bob + right, 10, 23 + bob + right, OUTLINE)
    rect(6, 17 + bob + right, 9, 22 + bob + right, TEAL)
    rect(23, 16 + bob + left, 27, 23 + bob + left, OUTLINE)
    rect(23, 17 + bob + left, 26, 22 + bob + left, TEAL)
    rect(9, 15 + bob, 24, 25 + bob, OUTLINE)
    rect(10, 16 + bob, 23, 23 + bob, DARK)
    rect(11, 17 + bob, 22, 21 + bob, TEAL)
    rect(14, 18 + bob, 19, 20 + bob, LIGHT)
    # Helmet, rim and antenna.
    rect(7, 6 + bob, 25, 17 + bob, OUTLINE)
    rect(9, 4 + bob, 23, 18 + bob, OUTLINE)
    rect(8, 7 + bob, 24, 15 + bob, TEAL)
    rect(10, 5 + bob, 22, 16 + bob, TEAL)
    rect(10, 6 + bob, 21, 8 + bob, LIGHT)
    rect(15, 2 + bob, 17, 5 + bob, DARK)
    rect(14, 1 + bob, 18, 3 + bob, ORANGE)
    if direction == 3:  # Back of helmet.
        rect(11, 10 + bob, 21, 14 + bob, DARK)
        rect(13, 11 + bob, 19, 12 + bob, LIGHT)
    elif direction in (1, 2):
        rect(7, 9 + bob, 17, 14 + bob, VISOR)
        rect(9, 10 + bob, 12, 12 + bob, LIGHT)
        rect(20, 10 + bob, 23, 13 + bob, DARK)
    else:
        rect(9, 9 + bob, 23, 14 + bob, VISOR)
        rect(11, 10 + bob, 14, 12 + bob, LIGHT)
        rect(18, 10 + bob, 21, 12 + bob, LIGHT)
    if direction == 2:
        pixels = [row[::-1] for row in pixels]
    return pixels


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def main():
    sheet = [[(0, 0, 0, 0)] * (SIZE * 4) for _ in range(SIZE * 4)]
    for direction in range(4):
        for phase in range(4):
            for y, row in enumerate(frame(direction, phase)):
                sheet[direction * SIZE + y][phase * SIZE:(phase + 1) * SIZE] = row
    raw = b"".join(b"\0" + bytes(channel for pixel in row for channel in pixel) for row in sheet)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 128, 128, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    (pathlib.Path(__file__).parent / "res" / "robot.png").write_bytes(png)


if __name__ == "__main__":
    main()
