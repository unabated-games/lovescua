"""Script validation must work without creating an engine/window or calling callbacks."""
import pathlib
import re
from collections import Counter
import subprocess
import sys
import tempfile

binary=str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='lovescua-check-') as directory:
    root=pathlib.Path(directory)
    (root/'scua.toml').write_text('[package]\nscua-version="0.38"\n')
    declarations=root/'decls'
    subprocess.run([binary,'--write-decls',str(declarations)],check=True,capture_output=True,timeout=15)
    for path in declarations.glob('*.scua'):
        counts=Counter(re.findall(r'^fn (\w+)\(',path.read_text(),re.MULTILINE))
        assert all(count==1 for count in counts.values()),(path.name,counts)
    cases=[
        ('definitions', 'import love\nrecord Config { size: vec2 }\nfn load() assert(false) end\nfn draw() love.graphics.clear(vec4(0,0,0,1)) end', None),
        ('engine guard', 'import love\nlove.graphics.new_canvas(1,1)', '--check does not execute engine functions'),
        ('syntax', 'fn broken(', 'lovescua:'),
        ('budget', 'while true do end', 'budget'),
    ]
    for label,source,error in cases:
        (root/'main.scua').write_text(source)
        run=subprocess.run([binary,'--check',str(root)],capture_output=True,text=True,timeout=10)
        if error is None:
            assert run.returncode==0 and 'without a window' in run.stdout,(label,run.stdout,run.stderr)
        else:
            assert run.returncode!=0 and error.lower() in run.stderr.lower(),(label,run.stdout,run.stderr)
    (root/'main.scua').write_text('import game_state\nfn load() assert(false) end')
    denied=subprocess.run([binary,'--check',str(root)],capture_output=True,text=True,timeout=10)
    assert denied.returncode!=0 and 'capability' in denied.stderr,denied.stderr
    granted=subprocess.run([binary,'--check','--save-dir',str(root/'saves'),str(root)],capture_output=True,text=True,timeout=10)
    assert granted.returncode==0 and 'without a window' in granted.stdout,granted.stderr
print('PASS: no-window definitions, uncalled callbacks, guards, budgets and opt-in save capability')
