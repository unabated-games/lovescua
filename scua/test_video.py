"""Window-free demo controls, or one short playback/rendering integration run."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

host = str(Path(sys.argv[1]).resolve())
mode = sys.argv[2]
example = Path(__file__).resolve().parents[1] / 'examples/video'
source = (example / 'main.scua').read_text()

if mode == '--controls':
    controls = source[source.index('fn restart()'):source.index('fn draw()')]
    script = '''let video=1
let ticks=0
let paused=false
let loops=0
let position=0.0
let playing=true
let quits=0
fn play(v) assert(v==video) playing=true end
fn pause(v) assert(v==video) playing=false end
fn seek(v,t) assert(v==video) position=t end
fn is_playing(v) assert(v==video) return playing end
fn advance(v,dt) if playing then position=position+dt end end
fn quit_game() quits=quits+1 end
let love={video={play=play,pause=pause,seek=seek,is_playing=is_playing,update=advance},event={quit=quit_game}}
''' + controls + '''
fn load()
  update(0.25)
  assert(position==0.25 and loops==0)
  keypressed("Space")
  assert(paused and not playing)
  update(0.25)
  assert(position==0.25 and loops==0,"manual pause must not auto-loop")
  keypressed("Space")
  assert(not paused and playing)
  keypressed("R")
  assert(position==0 and playing and not paused)
  position=8
  playing=false
  update(0.25)
  assert(position==0.25 and loops==1 and playing,"EOF must restart playback")
  keypressed("Space")
  keypressed("R")
  assert(position==0 and playing and not paused,"restart resumes from a pause")
  keypressed("Escape")
  keypressed("Unknown")
end
fn verify()
  assert(quits==1)
  print("PASS: video demo pause/resume, restart, EOF looping and quit")
end
'''
else:
    assert mode in ('metal', 'opengl')
    script = (source.replace('fn draw()', 'fn draw_example()')
              .replace('fn verify()', 'fn verify_example()')) + '''
let frames_checked=false
let demo_canvas=nil
let demo_first=nil
let demo_changed=false
fn frame_digest(probe,canvas)
  love.graphics.set_canvas(canvas)
  love.graphics.clear(vec4(0,0,0,1))
  love.video.draw(probe,vec2(0,0))
  love.graphics.set_canvas(nil)
  let pixels=love.graphics.texture_read(canvas)
  let digest=love.data.hash("sha256",love.image.bytes(pixels))
  love.object.close(pixels)
  return digest
end
fn draw()
  draw_example()
  if ticks==1 then
    demo_canvas=love.graphics.new_canvas(640,360)
    demo_first=frame_digest(video,demo_canvas)
    let encoded=nil
    match fs.read("res:motion.ogv")
      Ok(data) -> encoded=data
      Error(reason) -> assert(false,tostring(reason))
    end
    let probe=love.video.new(encoded,false)
    let canvas=love.graphics.new_canvas(640,360)
    assert(love.video.dimensions(probe)==vec2(640,360))
    love.video.play(probe)
    love.video.update(probe,0.05)
    let first=frame_digest(probe,canvas)
    for i in 0:8 do love.video.update(probe,0.25) end
    assert(love.video.is_playing(probe),"demo must last more than two seconds")
    let later=frame_digest(probe,canvas)
    assert(first!=later,"rendered video pixels must change over time")
    love.video.pause(probe)
    let stopped=love.video.tell(probe)
    love.video.update(probe,0.25)
    assert(love.video.tell(probe)==stopped and frame_digest(probe,canvas)==later)
    love.video.seek(probe,0)
    love.video.play(probe)
    love.video.update(probe,0.05)
    assert(frame_digest(probe,canvas)==first,"rewind must restore the initial video frame")
    for i in 0:40 do love.video.update(probe,0.25) end
    assert(not love.video.is_playing(probe),"decode must reach EOF")
    love.video.seek(probe,0)
    love.video.play(probe)
    love.video.update(probe,0.05)
    assert(love.video.is_playing(probe) and frame_digest(probe,canvas)==first,"video must restart after EOF")
    love.object.close(canvas)
    love.object.close(probe)
    frames_checked=true
  end
  if ticks==30 then
    demo_changed=frame_digest(video,demo_canvas)!=demo_first
    love.object.close(demo_canvas)
  end
end
fn verify()
  assert(demo_changed,"normal audio-synchronized demo frames must advance")
  assert(love.video.tell(video)>0,"normal playback clock must advance")
  verify_example()
  assert(frames_checked)
  print("PASS: changing video pixels, pause, rewind and restart after EOF")
end
'''

with tempfile.TemporaryDirectory(prefix='lovescua-video-') as directory:
    root = Path(directory)
    (root / 'scua.toml').write_text('[package]\nscua-version="0.38"\n')
    (root / 'main.scua').write_text(script)
    command = [host, '--no-input', '--verify']
    if mode == '--controls':
        command += ['--headless', '--frames', '1']
    else:
        (root / 'res').mkdir()
        shutil.copy2(example / 'res/motion.ogv', root / 'res/motion.ogv')
        command += ['--renderer', mode, '--frames', '30', '--fixed-dt', '0.016666666666666666']
    environment = dict(os.environ, ALSOFT_DRIVERS='null')
    run = subprocess.run([*command, str(root)], capture_output=True, text=True,
                         env=environment, timeout=20)
    assert run.returncode == 0, (run.stdout, run.stderr)
    print(run.stdout.strip())
