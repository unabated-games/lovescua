"""Native paths are explicit; ordinary assets, saves and reload retain their contracts."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile

host = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="lovescua-nativefs-") as directory:
    root = Path(directory).resolve()
    game = root / "game"
    (game / "res").mkdir(parents=True)
    saves = root / "saves"
    saves.mkdir()
    outside = root / "native files"
    outside.mkdir()
    (outside / "source.txt").write_text("native contents")
    (game / "res/asset.txt").write_text("resource contents")
    (root / "link").symlink_to(outside, target_is_directory=True)
    entry = game / "main.scua"
    native = json.dumps(str(outside), ensure_ascii=False)
    common = f'''import love
import nativefs as native
import filesystem
import bytes
import fs
import str
let directory={native}
fn must(result)
 match result Ok(value) -> return value Error(reason) -> assert(false,tostring(reason)) end
end
fn verify() end
'''
    entry.write_text(common + '''fn load()
 assert(not native.enabled())
 match pcall(fn() native.write(directory.."/source.txt","changed") end)
  Error(reason) -> assert(str.find(tostring(reason),"--allow-native-fs")!=nil)
  Ok(_) -> assert(false,"native writes require the host option")
 end
 assert(must(filesystem.read_text("asset.txt"))=="resource contents")
end
''')
    base = [host, "--headless", "--verify", "--frames", "1", "--save-dir", str(saves)]
    def run(extra=()):
        result = subprocess.run(base + list(extra) + [str(game)], cwd=root,
                                capture_output=True, text=True, timeout=15)
        assert result.returncode == 0, (result.stdout, result.stderr)
    run()
    assert (outside / "source.txt").read_text() == "native contents"
    entry.write_text(common + '''fn load()
 assert(native.enabled())
 assert(must(native.read_text(directory.."/source.txt"))=="native contents")
 assert(must(native.read_text("link/source.txt"))=="native contents")
 assert(native.absolute_path("game/../link/source.txt")==directory.."/source.txt")
 assert(native.cwd()!="" and native.exists(directory.."/source.txt"))
 must(native.create_directory(directory.."/nested"))
 must(native.write(directory.."/nested/data.bin",bytes.from_string("A\\0B")))
 assert(must(native.read(directory.."/nested/data.bin")).len()==3)
 must(native.append(directory.."/nested/data.bin","!"))
 let file=must(native.open(directory.."/nested/data.bin"))
 assert(native.stream.get_filename(file)==directory.."/nested/data.bin")
 assert(must(native.stream.get_size(file))==4)
 must(native.stream.seek(file,-1,"end"))
 assert(bytes.to_string(must(native.stream.read(file,1)))==Ok("!"))
 must(native.stream.close(file))
 match native.with_file(directory.."/scoped.txt","w",fn(handle)
   must(native.stream.set_buffer(handle,"full"))
   must(native.stream.write(handle,"scoped data"))
   assert(false,"deliberate body fault")
 end)
   Error(_) -> nil
   Ok(_) -> assert(false)
 end
 assert(must(native.read_text(directory.."/scoped.txt"))=="scoped data")
 must(native.rename(directory.."/scoped.txt",directory.."/renamed.txt"))
 assert(must(native.get_directory_items(directory)).len()>0)
 must(native.remove(directory.."/renamed.txt"))
 assert(not native.exists(directory.."/renamed.txt"))
 assert(must(filesystem.read_text("asset.txt"))=="resource contents")
 must(filesystem.write("save.txt","save contents"))
 assert(must(filesystem.read_text("user:save.txt"))=="save contents")
 print("PASS: opt-in native paths, relative/absolute/symlink paths, binary/stream/scoped I/O, assets and saves")
end
''')
    run(["--allow-native-fs"])
    assert (saves / "save.txt").read_text() == "save contents"
    assert (outside / "nested/data.bin").read_bytes() == b"A\0B!"
    # Every newly created reload partition must receive the explicit grant again.
    entry.write_text(common.replace('fn verify() end\n', '') + '''let ticks=0
fn load() assert(native.enabled()) end
fn snapshot() return tostring(ticks) end
fn restore(_) ticks=1 end
fn update(dt)
 ticks=ticks+1
 assert(must(native.read_text(directory.."/source.txt"))=="native contents")
 if ticks==1 then love.event.reload() end
end
fn verify() assert(ticks==3 and love.event.reload_status().error=="") end
''')
    result = subprocess.run([host, "--headless", "--allow-native-fs", "--verify", "--frames", "3", str(game)],
                            cwd=root, capture_output=True, text=True, timeout=15)
    assert result.returncode == 0 and "Reloaded game" in result.stdout, (result.stdout, result.stderr)
    print("PASS: native-path denial/default, explicit grant and reload grant retention")
