"""Exercise the real platform example's key handler without a window or dialogs."""
from pathlib import Path
import subprocess
import sys
import tempfile

host = str(Path(sys.argv[1]).resolve())
source = (Path(__file__).resolve().parents[1] / 'examples/platform/main.scua').read_text()
# Keep the actual handler under test; stub only its external OS actions. Calling
# with one SDL scancode name matches the host's native key dispatch contract.
handler = source[source.index('fn keypressed('):source.index('fn sensorupdated(')]
harness = '''let request=nil
let request_kind=""
let message="clipboard test"
let opened=0
let saved=0
let boxes=0
let copied=0
let urls=0
let reloads=0
let quits=0
fn chooser(kind: string, options)
  if kind=="open" then
    assert(options.multiple and options.filters[0].pattern=="png;jpg;qoi;exr")
    opened=opened+1
    return 1
  end
  assert(kind=="save" and options.default_name=="image.png")
  saved=saved+1
  return 2
end
fn box(title: string, text: string, options)
  assert(title=="LoveSCUA" and options.buttons[0]=="OK")
  boxes=boxes+1
end
fn clipboard(text: string)
  assert(text==message)
  copied=copied+1
end
fn url(text: string)
  assert(text=="https://github.com/unabated-games/lovescua")
  urls=urls+1
end
fn reload() reloads=reloads+1 end
fn quit_game() quits=quits+1 end
let love={
  window={show_file_dialog=chooser,show_message_box=box},
  system={set_clipboard_text=clipboard,open_url=url},
  event={reload=reload,quit=quit_game}
}
''' + handler + '''
fn load()
  keypressed("O")
  assert(opened==1 and request==1 and request_kind=="open")
  keypressed("S")
  assert(saved==1 and request==2 and request_kind=="save")
  keypressed("M")
  keypressed("C")
  keypressed("U")
  keypressed("R")
  keypressed("Escape")
  keypressed("Space")
end
fn verify()
  assert(boxes==1 and copied==1 and urls==1 and reloads==1 and quits==1)
  print("PASS: platform shortcuts accept one SDL key name and dispatch all actions")
end
'''
with tempfile.TemporaryDirectory(prefix='lovescua-platform-keys-') as directory:
    script = Path(directory) / 'main.scua'
    script.write_text(harness)
    run = subprocess.run([host, '--headless', '--frames', '1', '--verify', str(script)],
                         capture_output=True, text=True, timeout=10)
    assert run.returncode == 0, (run.stdout, run.stderr)
    print(run.stdout.strip())
