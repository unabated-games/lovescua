"""Regression checks for cross-module types, limits, cleanup and capability boundaries."""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave
import zlib


def main():
    executable = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="lovescua-native-") as directory:
        root = pathlib.Path(directory)
        (root / "res").mkdir()
        upstream = pathlib.Path(__file__).resolve().parents[1] / "testing/resources"
        for asset in ("font.ttf", "click.ogg", "sample.ogv"):
            (root / "res" / asset).write_bytes((upstream / asset).read_bytes())
        (root / "scua.toml").write_text('[package]\nscua-version = "0.37"\n')
        with wave.open(str(root / "res/tone.wav"), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(8000)
            output.writeframes(b"".join(struct.pack("<h", i * 10) for i in range(800)))
        for suffix, size in (("limit", 32 * 1024 * 1024), ("over", 32 * 1024 * 1024 + 1)):
            (root / ("res/" + suffix + ".zlib")).write_bytes(zlib.compress(bytes(size)))
        def run(name, code, error=None, extra=(), frames=2, draw=""):
            script = root / "main.scua"
            script.write_text("import love\nimport bytes\n" + code + "\nfn draw() " + draw + " end\n")
            result = subprocess.run([executable, "--no-input", "--frames", str(frames), *extra, str(script)],
                                    capture_output=True, text=True, timeout=20,
                                    env={**os.environ, "ALSOFT_DRIVERS": "null"})
            output = result.stdout + result.stderr
            assert result.returncode == (1 if error else 0), (name, output)
            if error:
                assert error.lower() in output.lower(), (name, output)
            print("PASS:", name)
        for name, code, error in [
            ("closed font", 'fn load() let f=love.graphics.new_font(20) love.graphics.release(f) love.graphics.font_width(f,"x") end', "stale"),
            ("cross-module type", 'fn load() let f=love.graphics.new_font(20) love.image.get_pixel(f,0,0) end', "userdata"),
            ("image pixel bounds", 'fn load() let d=love.image.new_data(2,2) love.image.get_pixel(d,2,0) end', "integer"),
            ("shader compilation", 'fn load() love.graphics.new_shader("this is not a shader") end', "shader"),
            ("unknown shader uniform", 'fn load() let s=love.graphics.new_shader("vec4 effect(vec4 c, Image t, vec2 uv, vec2 px){return c;}") love.graphics.shader_send(s,"missing",1) end', "absent"),
            ("random state validation", 'fn load() love.math.random_set_state(love.math.new_random(1),"invalid") end', "state"),
            ("curve bounds", 'fn load() let c=love.math.new_curve([vec2(0,0),vec2(1,1)]) love.math.curve_evaluate(c,2) end', "between 0 and 1"),
            ("triangulation type", 'fn load() love.math.triangulate([vec2(0,0),"invalid",vec2(1,1)]) end', "vec2"),
            ("invalid compression", 'fn load() love.data.compress("zip",bytes.from_string("x")) end', "unknown"),
            ("corrupt compressed data", 'fn load() love.data.decompress("zlib",bytes.from_string("bad")) end', "invalid compressed"),
            ("bounded LZ4 allocation", 'fn load() love.data.decompress("lz4",bytes.from_hex("ffffffff00")) end', "32 MiB"),
            ("bad encoding format", 'fn load() love.data.decode("binary","not-hex") end', "unknown"),
            ("physics dt limit", 'fn load() let w=love.physics.new_world(vec2(0,0)) love.physics.world_step(w,1) end', "range"),
            ("physics degenerate circle", 'fn load() let w=love.physics.new_world(vec2(0,0)) let b=love.physics.new_body(w,vec2(0,0),"dynamic") love.physics.new_circle(b,0.01,1) end', "tolerance"),
            ("physics cross-world joint", 'fn load() let a=love.physics.new_body(love.physics.new_world(vec2(0,0)),vec2(0,0),"dynamic") let b=love.physics.new_body(love.physics.new_world(vec2(0,0)),vec2(0,0),"dynamic") love.physics.new_distance_joint(a,b,vec2(0,0),vec2(0,1)) end', "same world"),
            ("mesh vertex validation", 'fn load() love.graphics.new_mesh([{position=vec2(0,0)}],"triangles") end', "mesh vertices"),
            ("particle capacity", 'fn load() love.graphics.new_particles(love.graphics.new_image_from_data(love.image.new_data(1,1)),100000) end', "integer"),
            ("event queue bounded", 'fn load() for i in 0:257 do love.event.post("x",i) end end', "256"),
            ("pending events on fault", 'fn load() love.event.post("x",{data=[1,2,3]}) assert(false) end', "assertion"),
            ("callback fault", 'fn load() love.event.post("x",1) end fn event(name,payload) assert(false) end', "event:"),
            ("joystick and touch polling", 'fn load() let n=love.joystick.count() assert(n>=0) assert(love.joystick.get(255)==nil) let t=love.touch.touches() assert(t.len()>=0) love.joystick.set_background_events(true) assert(love.joystick.background_events()) love.joystick.set_background_events(false) end', None),
            ("filesystem ambient path denial", 'import fs fn load() fs.read("/etc/passwd") end', "fs"),
            ("save capability required", 'import filesystem fn load() match filesystem.write("forbidden.txt","x") Ok(_) -> assert(false) Error(_) -> print("denied") end end', None),
            ("save traversal refused", 'import filesystem fn load() match filesystem.write("../escape","x") Ok(_) -> assert(false) Error(_) -> print("denied") end end', None),
        ]:
            run(name, code, error)
        run("canvas rejects ordinary textures", 'let image=nil fn load() image=love.graphics.new_image_from_data(love.image.new_data(1,1)) end', "requires a canvas", draw="love.graphics.set_canvas(image)")
        run("MRT duplicate rejected", 'let c=nil fn load() c=love.graphics.new_canvas(16,16) end', "duplicate render target",draw="love.graphics.set_canvases([c,c])")
        run("MRT stale texture rejected", 'let c=nil fn load() c=love.graphics.new_canvas(16,16) love.graphics.release(c) end', "live managed textures",draw="love.graphics.set_canvases([c])")
        run("MRT wrong type rejected", 'let c=nil fn load() c=love.graphics.new_font(16) end', "live managed textures",draw="love.graphics.set_canvases([c])")
        run("stencil comparison validation", '', "invalid stencil",draw='love.graphics.set_stencil_state("replace","invalid",1,255,255)')
        run("readback retains completed pixels", '''
let transfer=nil
fn load()
  let image=love.graphics.new_image_from_data(love.image.new_data(8,8))
  transfer=love.graphics.texture_readback_async(image)
  love.graphics.release(image)
end
fn verify_pixels()
  love.graphics.readback_wait(transfer)
  assert(love.graphics.readback_status(transfer)=="complete")
  let a=love.graphics.readback_image(transfer)
  let b=love.graphics.readback_image(transfer)
  love.graphics.release(transfer)
  love.graphics.release(a)
  assert(love.image.dimensions(b)==vec2(8,8))
  love.graphics.release(b)
end
let checked=false
fn update(dt) if not checked then verify_pixels() checked=true end end''')
        run("cursor ownership and aliases", '''
fn load()
  let image=love.image.new_data(16,16)
  for i in 0:16 do love.image.set_pixel(image,i,i,vec4(0,1,1,1)) end
  let cursor=love.mouse.new_cursor(image,0,0)
  assert(love.mouse.cursor_info(cursor).kind=="image")
  love.mouse.set_cursor(cursor)
  love.graphics.release(cursor)
  love.graphics.release(image)
  let alias=love.mouse.get_cursor()
  assert(love.mouse.cursor_info(alias).kind=="image")
  love.mouse.set_cursor(nil)
  assert(love.mouse.get_cursor()==nil)
  love.graphics.release(alias)
  let hand=love.mouse.system_cursor("hand")
  assert(love.mouse.cursor_info(hand).system=="hand")
  love.mouse.set_cursor(hand)
  love.graphics.release(hand)
end''')
        run("cursor hotspot bounds", 'fn load() love.mouse.new_cursor(love.image.new_data(8,8),8,0) end', "integer")
        run("system cursor validation", 'fn load() love.mouse.system_cursor("invalid") end', "unknown system cursor")
        shader='love.graphics.new_shader("extern vec4 colors[2]; vec4 effect(vec4 c,Image t,vec2 uv,vec2 px){return colors[0]+colors[1];}")'
        run("uniform array bound", 'fn load() let s='+shader+' love.graphics.shader_send_array(s,"colors",[vec4(1,1,1,1),vec4(1,1,1,1),vec4(1,1,1,1)]) end', "exceeds declared")
        run("uniform vector width", 'fn load() let s='+shader+' love.graphics.shader_send_array(s,"colors",[vec2(1,1)]) end', "component count")
        run("uniform matrix shape", 'fn load() let s=love.graphics.new_shader("extern mat2 matrix; vec4 effect(vec4 c,Image t,vec2 uv,vec2 px){return vec4(matrix*uv,0,1);}") love.graphics.shader_send_matrix(s,"matrix",[1,0,0]) end', "component count")
        run("integer vector uniforms", '''
fn load()
  let s=love.graphics.new_shader("extern ivec2 values[2]; extern bvec2 enabled; vec4 effect(vec4 c,Image t,vec2 uv,vec2 px){return vec4(float(values[0].x+values[1].y),enabled.x?1.0:0.0,enabled.y?1.0:0.0,1.0);}")
  love.graphics.shader_send_array(s,"values",[[1,2],[3,4]])
  love.graphics.shader_send_array(s,"enabled",[[true,false]])
end''')
        run("selected font retains aliases", '''
fn load()
  let f=love.graphics.new_font(20)
  love.graphics.set_font(f)
  love.graphics.release(f)
  let alias=love.graphics.get_font()
  assert(love.graphics.font_width(alias,"SCUA")>20)
  love.graphics.release(alias)
  love.graphics.set_font(nil)
  assert(love.graphics.font_metrics(love.graphics.get_font()).height>0)
end''')
        run("tiny fixed-step FPS remains bounded", "fn update(dt) assert(love.timer.get_fps()>0) end", extra=["--fixed-dt","1e-300"])
        run("upstream TrueType font resource", '''import fs
fn load()
  let encoded=nil
  match fs.read("res:font.ttf")
    Ok(value) -> encoded=value
    Error(reason) -> assert(false,tostring(reason))
  end
  let f=love.graphics.new_font_data(encoded,24)
  assert(love.graphics.font_has_glyphs(f,"Hello"))
  assert(love.graphics.font_width(f,"Hello")>20)
  love.graphics.set_font(f)
end''', draw='love.graphics.print("TrueType resource font",vec2(20,20))')
        if "--no-audio" not in sys.argv:
            run("WAV decoder, streaming, sample range", '''import fs
import love_audio as audio
import love_sound as sound
fn load()
  let encoded = nil
  match fs.read("res:tone.wav")
    Ok(value) -> encoded = value
    Error(error) -> assert(false,tostring(error))
  end
  let pcm=sound.decode(encoded)
  assert(sound.info(pcm).samples==800)
  assert(abs(sound.get_sample(pcm,1,1)-10/32767)<0.0001)
  let stream=audio.new_stream(encoded)
  assert(abs(audio.duration(stream)-0.1)<0.01)
  audio.set_volume(stream,0)
  assert(audio.play(stream))
  audio.pause(stream)
  assert(not audio.is_playing(stream))
  audio.stop(stream)
end''')
            run("upstream Ogg Vorbis resource", '''import fs
import love_audio as audio
import love_sound as sound
fn load()
  let encoded=nil
  match fs.read("res:click.ogg")
    Ok(value) -> encoded=value
    Error(reason) -> assert(false,tostring(reason))
  end
  let pcm=sound.decode(encoded)
  assert(sound.info(pcm).samples>0)
  assert(sound.info(pcm).duration>0)
  let stream=audio.new_stream(encoded)
  assert(audio.duration(stream)>0)
  audio.set_volume(stream,0)
  assert(audio.play(stream))
  audio.stop(stream)
end''')
            for label,code,error in [
                ("queue format mismatch", 'let q=audio.new_queue(8000,16,1,2) audio.queue(q,sound.new_data(10,16000,16,1))', "format"),
                ("queue buffer limit", 'audio.new_queue(8000,16,1,100)', "integer"),
                ("effect unknown parameter", 'audio.set_effect("bad","reverb",[{name="misspelled",value=1}])', "unknown effect parameter"),
                ("effect duplicate parameter", 'audio.set_effect("bad","reverb",[{name="gain",value=0.2},{name="gain",value=0.3}])', "duplicate effect parameter"),
                ("effect boolean type", 'audio.set_effect("bad","compressor",[{name="enable",value=1}])', "boolean"),
                ("filter gain range", 'let s=audio.new_source(sound.new_data(10,8000,16,1)) audio.set_filter(s,"lowpass",[{name="volume",value=2}])', "[0,1]"),
                ("parallel listener vectors", 'audio.set_listener_orientation(vec3(0,0,1),vec3(0,0,2))', "nonparallel"),
            ]:
                run(label, 'import love_audio as audio import love_sound as sound fn load() '+code+' end',error)
            run("video retains audio synchronization source", '''import fs
import love_audio as audio
import love_sound as sound
fn load()
  match fs.read("res:sample.ogv")
    Ok(encoded) -> do
      let video=love.video.new(encoded)
      let source=audio.new_source(sound.new_data(8000,8000,16,1))
      audio.set_volume(source,0)
      love.video.set_source(video,source)
      love.graphics.release(source)
      love.video.play(video)
      assert(love.video.is_playing(video))
      love.video.pause(video)
      assert(not love.video.is_playing(video))
      love.video.set_source(video,nil)
      love.video.play(video)
      love.video.update(video,0.1)
      assert(abs(love.video.tell(video)-0.1)<0.01)
    end
    Error(reason) -> assert(false,tostring(reason))
  end
end''')
            run("invalid audio data", 'import love_sound as sound fn load() sound.decode(bytes.from_string("bad")) end', "file type")
            run("sound sample range", 'import love_sound as sound fn load() sound.get_sample(sound.new_data(10,8000,16,1),10,1) end', "integer")
        run("sensor availability and shutdown", 'fn load() assert(love.sensor.has("accelerometer")==true or love.sensor.has("accelerometer")==false) assert(not love.sensor.is_enabled("accelerometer")) love.sensor.set_enabled("accelerometer",false) end')
        run("sensor type validation", 'fn load() love.sensor.has("compass") end', "accelerometer or gyroscope")
        run("disabled sensor data faults", 'fn load() love.sensor.data("gyroscope") end', "not enabled")
        run("video type validation", 'fn load() love.video.new(bytes.from_string("invalid")) end', "Ogg Theora")
        run("truncated video does not hang", 'import fs fn load() match fs.read("res:sample.ogv") Ok(data) -> love.video.new(bytes.slice(data,0,60)) Error(e) -> assert(false,tostring(e)) end end', "invalid")
        run("video step limit", 'import fs fn load() match fs.read("res:sample.ogv") Ok(data) -> love.video.update(love.video.new(data),1) Error(e) -> assert(false,tostring(e)) end end', "dt")
        run("physics callback lock protection", '''
let ball=nil
fn load()
  let w=love.physics.new_world(vec2(0,0))
  let ground=love.physics.new_body(w,vec2(0,0),"static")
  love.physics.new_box(ground,vec2(40,40),0)
  ball=love.physics.new_body(w,vec2(0,0),"dynamic")
  love.physics.new_circle(ball,10,1)
  love.physics.world_step(w,0.016)
end
fn begincontact(c) love.physics.body_destroy(ball) end''', "locked")
        run("physics callback fault unlocks world", '''
let reject=true
let saved=nil
fn begincontact(c)
  saved=c
  if reject then reject=false assert(false,"contact failure") end
end
fn load()
  let w=love.physics.new_world(vec2(0,0))
  let a=love.physics.new_body(w,vec2(0,0),"static")
  love.physics.new_box(a,vec2(40,40),0)
  let b=love.physics.new_body(w,vec2(0,0),"dynamic")
  love.physics.new_circle(b,10,1)
  match pcall(fn() love.physics.world_step(w,0.016) end)
    Ok(_) -> assert(false)
    Error(_) -> assert(love.physics.world_stats(w).bodies==2)
  end
  love.physics.body_destroy(b)
  assert(saved.body_a>0 and saved.body_b>0)
  assert(saved.points.len()>0)
  love.physics.world_step(w,0.016)
end''')
        run("physics destruction invalidates dependents", '''
fn load()
  let w=love.physics.new_world(vec2(0,0))
  let a=love.physics.new_body(w,vec2(0,0),"dynamic")
  let b=love.physics.new_body(w,vec2(100,0),"dynamic")
  let fixture=love.physics.new_circle(a,10,1)
  let joint=love.physics.new_distance_joint(a,b,vec2(0,0),vec2(100,0))
  love.physics.body_destroy(a)
  love.physics.body_destroy(a)
  assert(love.physics.body_is_destroyed(a))
  assert(love.physics.collider_is_destroyed(fixture))
  assert(love.physics.joint_is_destroyed(joint))
  assert(love.physics.world_stats(w).bodies==1)
  assert(love.physics.world_stats(w).joints==0)
  love.physics.collider_destroy(fixture)
  love.physics.joint_destroy(joint)
end''')
        run("physics destroyed access faults", 'fn load() let b=love.physics.new_body(love.physics.new_world(vec2(0,0)),vec2(0,0),"dynamic") love.physics.body_destroy(b) love.physics.body_position(b) end', "destroyed")
        run("physics publication rollback", '''
fn load()
  let w=love.physics.new_world(vec2(0,0))
  let a=love.physics.new_body(w,vec2(0,0),"dynamic")
  let b=love.physics.new_body(w,vec2(100,0),"dynamic")
  let retained=[]
  for i in 0:4093 do retained.push(love.math.new_random(i)) end
  match pcall(fn() return love.physics.new_body(w,vec2(0,0),"dynamic") end)
    Ok(_) -> assert(false)
    Error(_) -> assert(love.physics.world_stats(w).bodies==2)
  end
  match pcall(fn() return love.physics.new_distance_joint(a,b,vec2(0,0),vec2(100,0)) end)
    Ok(_) -> assert(false)
    Error(_) -> assert(love.physics.world_stats(w).joints==0)
  end
  match pcall(fn() return love.physics.new_circle(a,10,1) end)
    Ok(_) -> assert(false)
    Error(_) -> assert(love.physics.world_stats(w).fixtures==0)
  end
  assert(retained.len()==4093)
end''')
        for name, expected, error in (("limit", 32 * 1024 * 1024, None), ("over", 0, "32 MiB")):
            run("decompression output " + name, '''import fs
fn load()
  let encoded=nil
  match fs.read("res:''' + name + '''.zlib")
    Ok(value) -> encoded=value
    Error(reason) -> assert(false,tostring(reason))
  end
  let decoded=love.data.decompress("zlib",encoded)
  assert(decoded.len()==''' + str(expected) + ''')
end''', error)
        run("polygon edge and chain shapes", '''
fn load()
  let w=love.physics.new_world(vec2(0,0))
  let b=love.physics.new_body(w,vec2(50,50),"dynamic")
  let f=love.physics.new_polygon(b,[vec2(-20,-20),vec2(20,-20),vec2(0,20)],1)
  assert(love.physics.collider_test_point(f,vec2(50,50)))
  let mass=love.physics.body_mass(b)
  love.physics.collider_set_density(f,2)
  assert(abs(love.physics.body_mass(b)-mass*2)<0.001)
  love.physics.collider_set_sensor(f,true)
  assert(love.physics.collider_info(f).sensor)
  assert(love.physics.collider_info(f).density==2)
  love.physics.body_set_enabled(b,false)
  assert(not love.physics.body_is_enabled(b))
  love.physics.body_set_enabled(b,true)
  love.physics.body_set_type(b,"kinematic")
  assert(love.physics.body_type(b)=="kinematic")
  let floor=love.physics.new_body(w,vec2(0,100),"static")
  love.physics.new_edge(floor,vec2(-100,0),vec2(100,0))
  love.physics.new_chain(floor,[vec2(-100,0),vec2(0,-10),vec2(100,0)],false)
  love.physics.new_chain(floor,[vec2(-50,-50),vec2(50,-50),vec2(50,50),vec2(-50,50)],true)
  love.physics.world_step(w,0.016)
end''')
        run("degenerate polygon refused", 'fn load() let b=love.physics.new_body(love.physics.new_world(vec2(0,0)),vec2(0,0),"dynamic") love.physics.new_polygon(b,[vec2(0,0),vec2(1,1),vec2(2,2)],1) end', "convex")
        run("degenerate chain refused", 'fn load() let b=love.physics.new_body(love.physics.new_world(vec2(0,0)),vec2(0,0),"static") love.physics.new_chain(b,[vec2(0,0),vec2(0,0)],false) end', "separated")
        save = root / "saves"
        run("granted save traversal refused", 'import filesystem fn load() match filesystem.write("../escape","x") Ok(_) -> assert(false) Error(_) -> print("denied") end end', None, ["--save-dir", str(save)])
        assert not (root / "escape").exists()
        assert not (root / "forbidden.txt").exists()
        print("PASS: native validation and cleanup")


if __name__ == "__main__":
    main()
