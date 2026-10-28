"""Build a pinned SCUA embedding SDK (0.39 or its preview, ABI 3) without changing its checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True,help='SCUA git checkout')
parser.add_argument('--revision',default='HEAD')
parser.add_argument('--output',type=Path,default=Path('build/scua-sdk-0.39-preview'))
parser.add_argument('--zig',default='zig',help='Zig 0.16 executable')
parser.add_argument('--target',help='optional Zig target triple/minimum OS')
parser.add_argument('--native-fs-root-fix',action='store_true',help='apply the local filesystem-root grant fix to the pinned preview')
args=parser.parse_args()
source=args.source.resolve()
revision=subprocess.check_output(['git','rev-parse',args.revision],cwd=source,text=True).strip()
header=subprocess.check_output(['git','show',revision+':include/scua.h'],cwd=source,text=True)
def define(name):
    found=re.search(r'^#define '+name+r'\s+(\d+)\s*$',header,re.M)
    if not found: raise SystemExit('Missing '+name+' at '+revision)
    return int(found[1])
version=tuple(define('SCUA_VERSION_'+part) for part in ('MAJOR','MINOR','PATCH'))
if version not in ((0,38,0),(0,39,0)) or define('SCUA_ABI_VERSION')!=3 or define('SCUA_FAULT_ERROR')!=3:
    raise SystemExit('Expected SCUA 0.39 or its embedding preview (ABI 3) at '+revision)
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
patches=[]
if args.native_fs_root_fix:
    patch=Path(__file__).resolve().parent/'sdk_patches/native_fs_root.patch'
    subprocess.run(['patch','-p1','--batch','--forward','--fuzz=0','--input',str(patch)],cwd=work,check=True)
    patches.append({'name':patch.name,'sha256':hashlib.sha256(patch.read_bytes()).hexdigest()})
command=[args.zig,'build','sdk','-Doptimize=ReleaseFast','-Ddev=false','-j2',
         '--cache-dir',str(output.parent/'scua-zig-cache'),
         '--global-cache-dir',str(output.parent/'scua-zig-global'),
         '--prefix',str(output)]
if args.target: command.append('-Dtarget='+args.target)
subprocess.run(command,cwd=work,check=True)
shutil.copy2(work/'packaging/LICENSE',output/'LICENSE')
(output/'BUILD.json').write_text(json.dumps({'source_commit':revision,'version':'.'.join(map(str,version)),'abi':3,
                                           'zig':zig_version,'optimize':'ReleaseFast','ffi':False,
                                           'target':args.target or 'native','patches':patches},indent=2)+'\n')
print('Built',output)
