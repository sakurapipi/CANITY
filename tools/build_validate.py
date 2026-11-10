"""ASCII-only, incremental C99 build and real-runtime validation.

工具链解析（本地与 CI 同源）：
  CANITY_GCC      gcc 可执行路径；未设则在 PATH 里找 gcc
  CANITY_WINDRES  windres 可执行路径；未设则在 PATH 里找 windres
本地以前把这个路径写死在源码里 —— 那样脚本没法入库，CI 也跑不了。
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
import json
import tempfile
import time

root = Path(__file__).resolve().parents[1]
os.chdir(root)

parser = argparse.ArgumentParser()
parser.add_argument('--build-only', action='store_true')
args = parser.parse_args()


def tool(env_name, exe):
    """CANITY_<X> 环境变量优先，其次 PATH。找不到就明确失败。"""
    v = os.environ.get(env_name)
    if v:
        p = Path(v)
        if p.is_file():
            return str(p)
        raise SystemExit('ERROR: %s points at a missing file: %s' % (env_name, v))
    w = shutil.which(exe)
    if w:
        return w
    raise SystemExit('ERROR: cannot find %s; set %s to its full path' % (exe, env_name))


GCC = tool('CANITY_GCC', 'gcc')
WINDRES = tool('CANITY_WINDRES', 'windres')

# 唯一的源文件清单来源：CI 与 Release 都读它，避免三边命令漂移。
sys.path.insert(0, str(root / 'tools'))
from build_sources import game_sources, test_canity_sources  # noqa: E402
from build_cache import file_digest, inputs_digest, include_digest, tool_identity, read_profile, write_profile, validation_digest  # noqa: E402

cache = root / 'work' / 'codex-build'
cache.mkdir(parents=True, exist_ok=True)

compile_flags = ['-Iinclude', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror']
sources = game_sources(root)
profile_path = cache / 'compile-profile.json'
previous = read_profile(profile_path)
configuration = dict(compiler=tool_identity(GCC), flags=compile_flags, working_directory=str(root),
                     includes=include_digest(root))
source_hashes = {src.relative_to(root).as_posix(): file_digest(src) for src in sources}
rebuild_all = previous.get('configuration') != configuration
previous_sources = previous.get('sources', {})
if not isinstance(previous_sources, dict):
    previous_sources = {}


results=[]
def run(cmd,env=None,label=None,timeout=300):
    command=[str(x) for x in cmd];label=label or ' '.join(command[:2])
    start=time.monotonic();log=cache/('validation-%03d.log'%len(results))
    try:
        proc=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=timeout)
        output=proc.stdout;code=proc.returncode
    except subprocess.TimeoutExpired as exc:
        output=(exc.stdout or b'')+b'\nTIMEOUT\n';code=124
    except OSError as exc:
        output=str(exc).encode('utf-8');code=127
    log.write_bytes(output)
    results.append(dict(stage=label,returncode=code,seconds=round(time.monotonic()-start,3),log=log.relative_to(root).as_posix()))
    if output:print(output.decode('utf-8',errors='replace'),end='',flush=True)
    if code:print('FAILED: %s (%d); continuing independent stages'%(label,code),flush=True)
    return code==0

def finish():
    report=dict(passed=all(item['returncode']==0 for item in results),build_only=args.build_only,acceptance_tests_run=not args.build_only,
                source_inputs_sha256=inputs_digest(root,sources),include_sha256=include_digest(root),validation_inputs_sha256=validation_digest(root),stages=results)
    (cache/'validation-summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(('BUILD SUMMARY' if args.build_only else 'VALIDATION SUMMARY')+': %d passed / %d failed'%(sum(r['returncode']==0 for r in results),sum(r['returncode']!=0 for r in results)))
    raise SystemExit(0 if report['passed'] else 1)


print('toolchain: gcc=%s' % GCC, flush=True)
print('toolchain: windres=%s' % WINDRES, flush=True)

# ---- 资源：图标、清单或 windres 改变也不能复用旧资源 ----
res_o = root / 'res' / 'canity_res.o'
rc = root / 'res' / 'canity.rc'
resource_inputs = [p for p in (root / 'res').rglob('*')
                   if p.is_file() and p.suffix.lower() in {'.rc', '.h', '.ico', '.manifest', '.bmp', '.png'}]
resources = dict(compiler=tool_identity(WINDRES), inputs=inputs_digest(root, resource_inputs))
if not res_o.exists() or previous.get('resources') != resources:
    resources_ok=run([WINDRES, rc, '-O', 'coff', '-o', res_o],label='resources')
else:
    resources_ok=True

# ---- 逐个编译（增量）----
objects = []
objects_ok=True
compiled_sources = {} if rebuild_all else dict(previous_sources)
for src in sources:
    obj = cache / (str(src.relative_to(root)).replace('\\', '_').replace('/', '_') + '.o')
    source_key = src.relative_to(root).as_posix()
    if (not obj.exists() or rebuild_all
            or previous_sources.get(source_key) != source_hashes[source_key]):
        if not run([GCC, '-c', src, *compile_flags, '-o', obj],label='compile '+source_key):
            objects_ok=False
            continue
    compiled_sources[source_key] = source_hashes[source_key]
    write_profile(profile_path, dict(configuration=configuration, sources=compiled_sources, resources=resources))
    objects.append(obj)

libs = ['-lgdi32', '-luser32', '-lwinmm']
game_ok=objects_ok and resources_ok and run([GCC,*objects,res_o,*libs,'-mwindows','-o',root/'canity_verify.exe'],label='link game')
if game_ok:
    write_profile(profile_path,dict(configuration=configuration,sources=source_hashes,resources=resources))
    print('C99 game build: PASS',flush=True)
else:
    profile_path.unlink(missing_ok=True)
    print('C99 game build: FAILED; old executable is not approved for packaging',flush=True)
if args.build_only:finish()

# Each executable is run only after its own successful compilation.
smoke=cache/'test_canity.exe'
if run([GCC,*test_canity_sources(root),'-Iinclude','-std=c99','-Wall','-Wextra','-o',smoke,'-lm'],label='compile offline contracts'):
    run([smoke],label='offline contracts')

rules=cache/'test_v5_rules.exe'
rule_sources=['tests/test_v5_rules.c','src/perception.c','src/map_topology.c','src/room_material.c','src/monster_rules.c','src/room_template.c']
if run([GCC,*rule_sources,'-Iinclude','-std=c99','-Wall','-Wextra','-o',rules,'-lm'],label='compile gameplay rules'):
    run([rules],label='gameplay rules')

# Run every runtime domain in its own process/directory so a failing assertion
# cannot hide another domain or contaminate its save state.
suites=('base','phase','hatch','collapse','throwing','core','ui','update','progress','actors','world','save','delivery','existing_save','legacy_fixtures')
runtime=cache/'test_runtime.exe'
test_srcs=['tests/test_runtime.c']+sorted(str(q) for q in (root/'tests/domains').glob('*.c'))
if objects_ok and run([GCC,*test_srcs,*objects,'-Iinclude','-std=c99','-Wall','-Wextra',*libs,'-o',runtime],label='compile runtime domains'):
    for suite in suites:
        with tempfile.TemporaryDirectory(prefix='runtime-'+suite+'-',dir=cache) as temp:
            env=dict(os.environ,CANITY_DATA_DIR=temp)
            if suite=='existing_save':
                # CI always has a deterministic synthetic input. A private user
                # save is optional and explicitly identified, never implied.
                source=Path(os.environ.get('CANITY_EXISTING_SAVE') or root/'tests/fixtures/saves/synthetic_v44.sav')
                copied=Path(temp)/'input.sav'
                try:shutil.copy2(source,copied)
                except OSError as exc:
                    results.append(dict(stage='runtime existing_save fixture',returncode=2,error=str(exc)))
                    continue
                env['CANITY_EXISTING_SAVE']=str(copied)
                print('existing_save input: '+('explicit user copy' if os.environ.get('CANITY_EXISTING_SAVE') else 'synthetic v44 contract fixture'),flush=True)
            run([runtime,'--suite',suite],env,label='runtime '+suite,timeout=180)
else:
    for suite in suites:results.append(dict(stage='runtime '+suite,returncode=2,error='runtime executable unavailable after compilation failure'))

for script in ('partition_check.py','content_check.py','skill_layout_check.py','hotkey_check.py','stub_scan.py'):
    run([sys.executable,'-X','utf8','tools/'+script],label=script)
finish()
