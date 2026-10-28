"""PCM frames/channels, copy isolation, and sample/second source units (silent backend)."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

host = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="lovescua-audio-behavior-") as directory:
    entry = Path(directory) / "main.scua"
    entry.write_text('''import love_sound as sound
import love_audio as audio
fn near(a,b) return abs(a-b)<0.0001 end
fn fails(body) match pcall(body) Error(_) -> nil Ok(_) -> assert(false,"expected failure") end end
fn load()
 let stereo=sound.new_data(16000,8000,16,2)
 let defaults=sound.info(sound.new_data(16))
 assert(defaults.sample_rate==44100 and defaults.bit_depth==16 and defaults.channels==2)
 sound.set_sample(stereo,0,1,0.5)
 sound.set_sample(stereo,0,2,-0.5)
 assert(near(sound.get_sample(stereo,0,1),0.5) and near(sound.get_sample(stereo,0,2),-0.5))
 assert(near(sound.get_sample(stereo,0),0.5) and near(sound.get_sample(stereo,1),-0.5))
 sound.set_sample(stereo,2,0.75)
 assert(near(sound.get_sample(stereo,1,1),0.75),"interleaved sample maps to frame/channel")
 assert(sound.info(stereo).samples==16000 and sound.info(stereo).channels==2)
 let cloned=sound.clone(stereo)
 sound.set_sample(cloned,0,1,0.25)
 assert(near(sound.get_sample(stereo,0,1),0.5),"PCM clone must be independent")
 let sliced=sound.slice_data(stereo,0,2)
 assert(sound.info(sliced).samples==2 and near(sound.get_sample(sliced,0,2),-0.5))
 assert(sound.info(sound.slice_data(stereo,15998)).samples==2)
 let copied=sound.new_data(4,8000,16,2)
 sound.copy_data(copied,stereo,0,2)
 assert(near(sound.get_sample(copied,0,2),-0.5) and near(sound.get_sample(copied,1,1),0.75))
 sound.copy_data(copied,copied,0,2,1)
 assert(near(sound.get_sample(copied,1,2),-0.5) and near(sound.get_sample(copied,2,1),0.75),"overlapping PCM copy")
 fails(fn() sound.get_sample(stereo,0,0) end)
 let source=audio.new_source(stereo)
 assert(near(audio.duration(source),2) and near(audio.duration(source,"samples"),16000))
 audio.seek(source,4000,"samples")
 assert(near(audio.tell(source),0.5) and near(audio.tell(source,"samples"),4000))
 audio.seek(source,1)
 assert(near(audio.tell(source,"samples"),8000))
 fails(fn() audio.seek(source,0,"invalid") end)
 assert(near(audio.tell(source),1),"invalid unit must not change position")
 print("PASS: PCM frame/channel convention, clone/slice isolation and audio samples/seconds units")
end
fn verify() end
''')
    result = subprocess.run([host,"--headless","--verify","--frames","1",str(entry)], capture_output=True,text=True,timeout=15,
                            env={**os.environ,"ALSOFT_DRIVERS":"null"})
    assert result.returncode == 0, (result.stdout,result.stderr)
    print(result.stdout.strip())
