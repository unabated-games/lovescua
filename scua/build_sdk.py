"""Build an immutable local SCUA 0.38 / ABI 3 SDK without changing its checkout."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True,help='SCUA git checkout')
parser.add_argument('--revision',default='HEAD')
parser.add_argument('--output',type=Path,default=Path('build/scua-sdk-0.38'))
parser.add_argument('--zig',default='zig',help='Zig 0.16 executable')
parser.add_argument('--target',help='optional Zig target triple/minimum OS')
args=parser.parse_args()
source=args.source.resolve()
revision=subprocess.check_output(['git','rev-parse',args.revision],cwd=source,text=True).strip()
header=subprocess.check_output(['git','show',revision+':include/scua.h'],cwd=source,text=True)
for name,value in [('SCUA_VERSION_MAJOR',0),('SCUA_VERSION_MINOR',38),('SCUA_VERSION_PATCH',0),('SCUA_ABI_VERSION',3)]:
    if not re.search(r'^#define '+name+r'\s+'+str(value)+r'\s*$',header,re.M):
        raise SystemExit('Expected SCUA 0.38.0 ABI 3 at '+revision)
zig_version=subprocess.check_output([args.zig,'version'],text=True).strip()
if not zig_version.startswith('0.16.'):
    raise SystemExit('This SCUA source uses Zig 0.16.x; select it with --zig (found '+zig_version+')')
output=args.output.resolve()
work=output.parent/('scua-source-'+revision[:12])
work.mkdir(parents=True,exist_ok=True)
archive=output.parent/('scua-source-'+revision[:12]+'.tar')
with archive.open('wb') as stream:
    subprocess.run(['git','archive',revision,'build.zig','src','include','packaging','examples'],cwd=source,stdout=stream,check=True)
with tarfile.open(archive) as bundle:
    bundle.extractall(work,filter='data')
command=[args.zig,'build','sdk','-Doptimize=ReleaseFast','-Ddev=false','-j2',
         '--cache-dir',str(output.parent/'scua-zig-cache'),
         '--global-cache-dir',str(output.parent/'scua-zig-global'),
         '--prefix',str(output)]
if args.target: command.append('-Dtarget='+args.target)
subprocess.run(command,cwd=work,check=True)
shutil.copy2(work/'packaging/LICENSE',output/'LICENSE')
(output/'BUILD.json').write_text(json.dumps({'source_commit':revision,'version':'0.38.0','abi':3,
                                           'zig':zig_version,'optimize':'ReleaseFast','ffi':False,
                                           'target':args.target or 'native'},indent=2)+'\n')
print('Built',output)
