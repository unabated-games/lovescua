"""Generate an original finite four-channel ProTracker MOD with one embedded sample."""
import math
import pathlib
import struct
# 31 sample descriptors, only sample 1 populated; 32 bytes of signed 8-bit sine.
sample=bytes(round(80*math.sin(i*math.tau/32))&255 for i in range(32))
header=b'SCUA tracker fixture'.ljust(20,b'\0')
for i in range(31):
    header+=(b'sine' if i==0 else b'').ljust(22,b'\0')
    header+=struct.pack('>HBBHH',len(sample)//2 if i==0 else 0,0,48 if i==0 else 0,0,1)
header+=bytes([1,0])+bytes(128)+b'M.K.'
pattern=bytearray(64*4*4)
# C-3, instrument 1. End the song at the second row with F00 (finite decoder).
period=428
pattern[:4]=bytes([period>>8,period&255,0x10,0])
pattern[16:20]=bytes([0,0,0x0f,0])
assert len(header)==1084
(pathlib.Path(__file__).resolve().parent/'res/tone.mod').write_bytes(header+pattern+sample)
