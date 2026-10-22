"""Real host lifecycle checks; all scenes use CPU bindings and open no windows."""
import pathlib
import subprocess
import sys
import tempfile

host = str(pathlib.Path(sys.argv[1]).resolve())
script = '''import love
import fs
import json
let ticks=0
let revision="REVISION"
fn load()
  let image=love.image.new_data(4,4,"rgba32f")
  LOAD_BODY
end
fn snapshot() return json.encode({ticks=ticks}) end
fn restore(encoded: string)
  RESTORE_BODY
  match json.decode(encoded)
    Ok(saved) -> ticks=saved.ticks
    Error(reason) -> assert(false,tostring(reason))
  end
end
fn update(dt: float)
  ticks=ticks+1
  if ticks==1 then
    print("READY")
  end
  if ticks==2 then
    let ready=false
    for i in 0..500 do
      match fs.read_text("res:continue")
        Ok(_) -> do ready=true break end
        Error(_) -> love.timer.sleep(0.01)
      end
    end
    assert(ready,"test replacement handshake timed out")
    love.event.reload()
  end
end
fn verify()
  assert(ticks==3,"logical state preserved")
  assert(revision=="EXPECTED","correct game remained active")
  assert((love.event.reload_status().error!="")==ERROR_EXPECTED,"reload error status")
  print("PASS lifecycle")
end
fn quit() print("QUIT lifecycle") end
'''

# Avoid replacement overlap between EXPECTED and ERROR_EXPECTED.
def make(revision, expected, error=False, load='nil', restore='nil'):
    return (script.replace('ERROR_EXPECTED', str(error).lower())
            .replace('REVISION', revision).replace('EXPECTED', expected)
            .replace('LOAD_BODY', load).replace('RESTORE_BODY', restore))

with tempfile.TemporaryDirectory(prefix='lovescua-lifecycle-') as root:
    game = pathlib.Path(root) / 'main.scua'
    resources_ready = pathlib.Path(root) / 'res'
    resources_ready.mkdir()
    signal = resources_ready / 'continue'
    def replace_source(path, source):
        pending = path.with_suffix('.tmp')
        pending.write_text(source)
        pending.replace(path)
    cases = [
        ('success', make('v1', 'v2'), make('v2', 'v2')),
        ('syntax rollback', make('v1', 'v1', True), 'fn broken(\n'),
        ('load rollback', make('v1', 'v1', True), make('v2', 'v2', load='assert(false,"deliberate load fault")')),
        ('restore rollback', make('v1', 'v1', True), make('v2', 'v2', restore='assert(false,"deliberate restore fault")')),
        ('post-commit callback fault', make('v1', 'v2'), make('v2', 'v2', True) + '\nfn reloaded() assert(false,"deliberate post-commit fault") end\n'),
    ]
    for name, initial, replacement in cases:
        signal.unlink(missing_ok=True)
        game.write_text(initial)
        process = subprocess.Popen([host, '--headless', '--frames', '3', '--verify', str(game)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        line = process.stdout.readline()
        if line.strip() != 'READY':
            out, err = process.communicate(timeout=5)
            raise AssertionError((name, line, out, err))
        replace_source(game, replacement)
        signal.write_text('ready')
        out, err = process.communicate(timeout=8)
        assert process.returncode == 0 and 'PASS lifecycle' in out and 'QUIT lifecycle' in out, (name, out, err)
        if name not in ('success', 'post-commit callback fault'):
            assert 'reload rejected; current game continues' in err, (name, err)

    signal.unlink(missing_ok=True)
    helper = pathlib.Path(root) / 'behavior.scua'
    helper.write_text('fn value() return "v1" end\nreturn {value=value}\n')
    watched = make('v1', 'v2').replace('let revision="v1"', 'import behavior\nlet revision=behavior.value()').replace('    love.event.reload()\n', '')
    game.write_text(watched)
    process = subprocess.Popen([host, '--headless', '--watch', '--frames', '3', '--verify', str(game)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    line = process.stdout.readline()
    assert line.strip() == 'READY', line
    replace_source(helper, 'fn value() return "v2" end\nreturn {value=value}\n')
    signal.write_text('ready')
    out, err = process.communicate(timeout=8)
    assert process.returncode == 0 and 'PASS lifecycle' in out and 'QUIT lifecycle' in out, (out, err)

    resources = pathlib.Path(root) / 'mounted'
    resources.mkdir()
    (resources / 'value.txt').write_text('mounted resource')
    game.write_text('import fs\nfn load()\n  match fs.read_text("res:value.txt")\n    Ok(value) -> assert(value=="mounted resource")\n    Error(reason) -> assert(false,tostring(reason))\n  end\nend\n')
    mounted = subprocess.run([host, '--headless', '--mount', str(resources), str(game)], capture_output=True, text=True, timeout=5)
    assert mounted.returncode == 0, (mounted.stdout, mounted.stderr)

    game.write_text('import love\nfn load() love.event.quit() end\nfn verify() end\n')
    early = subprocess.run([host, '--headless', '--verify', '--frames', '3', str(game)], capture_output=True, text=True, timeout=5)
    assert early.returncode != 0 and 'quit before requested frame count' in early.stderr, (early.stdout, early.stderr)

    bad = pathlib.Path(root) / 'bad_types.scua'
    bad.write_text('''import handles
fn wants_image(value: handles.ImageData) return value.raw end
fn bad(value: handles.Texture) return wants_image(value) end
''')
    result = subprocess.run([host, '--check', str(bad)], capture_output=True, text=True, timeout=5)
    assert result.returncode != 0 and 'expected ImageData' in result.stderr, result
print('PASS: changed code commits; syntax/load/restore failures roll back; resource brands reject mixing')
