"""The pinned tracker decoder must not import ambient file-opening functions."""
import pathlib
import re
import subprocess
import sys
library=pathlib.Path(sys.argv[1])
result=subprocess.run(['nm','-u',str(library)],check=True,capture_output=True,text=True)
assert not re.search(r'\bU\s+_(?:fopen|freopen|open|openat|popen|system)\b',result.stdout),result.stdout
print('PASS: bundled tracker archive has no ambient file/process opening imports')
