"""Deterministic 4x4 red DDS/DXT1 and ASTC blocks; no external image tools."""
import pathlib
import struct
root=pathlib.Path(__file__).resolve().parent/'res'
# DDS_HEADER with one DXT1 block. Every 2-bit palette index selects red 0xf800.
header=[124,0x81007,4,4,8,0,0]+[0]*11+[32,4,int.from_bytes(b'DXT1','little'),0,0,0,0,0]+[0x1000,0,0,0,0]
assert len(header)==31
(root/'red.dds').write_bytes(b'DDS '+struct.pack('<31I',*header)+struct.pack('<HHI',0xf800,0,0))
# ASTC LDR void-extent constant red, with unspecified extent coordinates.
(root/'red.astc').write_bytes(bytes.fromhex('13aba15c040401040000040000010000fcfdffffffffffffffff00000000ffff'))
