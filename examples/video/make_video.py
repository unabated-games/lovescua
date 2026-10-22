"""Regenerate the motion clip with libtheora, then mux quiet Vorbis ticks.

Requires a C compiler, pkg-config, libtheora/libogg and FFmpeg. These tools are
only needed to rebuild the checked-in asset, not to build or run LoveSCUA.
"""
from pathlib import Path
import math
import shlex
import struct
import subprocess
import tempfile
import wave

root = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='lovescua-make-video-') as directory:
    temporary = Path(directory)
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'theoraenc', 'ogg'], text=True))
    encoder = temporary / 'encode'
    subprocess.run(['cc', str(root / 'make_video.c'), '-O2', '-o', str(encoder), *flags, '-lm'], check=True)
    silent = temporary / 'silent.ogv'
    subprocess.run([str(encoder), str(silent)], check=True)
    audio = temporary / 'ticks.wav'
    with wave.open(str(audio), 'wb') as output:
        output.setparams((2, 2, 22050, 0, 'NONE', 'not compressed'))
        for sample in range(8 * 22050):
            phase = (sample % 22050) / 22050
            envelope = max(0, 1 - phase / 0.06) if phase < 0.06 else 0
            value = int(1200 * envelope * math.sin(sample * 2 * math.pi * 440 / 22050))
            output.writeframesraw(struct.pack('<hh', value, value))
    destination = root / 'res/motion.ogv'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', str(silent), '-i', str(audio),
                    '-map', '0:v:0', '-map', '1:a:0', '-c:v', 'copy', '-c:a', 'vorbis',
                    '-strict', '-2', '-b:a', '48k', '-shortest', str(destination)], check=True)
    print(f'Wrote {destination.name}: {destination.stat().st_size} bytes')
