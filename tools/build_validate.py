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
from build_cache import file_digest, inputs_digest, include_digest, tool_identity, read_profile, write_profile  # noqa: E402

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


def run(cmd, env=None):
    p = subprocess.run([str(x) for x in cmd], env=env)
    if p.returncode:
        raise SystemExit(p.returncode)


print('toolchain: gcc=%s' % GCC, flush=True)
print('toolchain: windres=%s' % WINDRES, flush=True)

# ---- 资源：图标、清单或 windres 改变也不能复用旧资源 ----
res_o = root / 'res' / 'canity_res.o'
rc = root / 'res' / 'canity.rc'
resource_inputs = [p for p in (root / 'res').rglob('*')
                   if p.is_file() and p.suffix.lower() in {'.rc', '.h', '.ico', '.manifest', '.bmp', '.png'}]
resources = dict(compiler=tool_identity(WINDRES), inputs=inputs_digest(root, resource_inputs))
if not res_o.exists() or previous.get('resources') != resources:
    run([WINDRES, rc, '-O', 'coff', '-o', res_o])
    print('resource: rebuilt canity_res.o', flush=True)

# ---- 逐个编译（增量）----
objects = []
compiled_sources = {} if rebuild_all else dict(previous_sources)
for src in sources:
    obj = cache / (str(src.relative_to(root)).replace('\\', '_').replace('/', '_') + '.o')
    source_key = src.relative_to(root).as_posix()
    if (not obj.exists() or rebuild_all
            or previous_sources.get(source_key) != source_hashes[source_key]):
        run([GCC, '-c', src, *compile_flags, '-o', obj])
    compiled_sources[source_key] = source_hashes[source_key]
    write_profile(profile_path, dict(configuration=configuration, sources=compiled_sources, resources=resources))
    objects.append(obj)

libs = ['-lgdi32', '-luser32', '-lwinmm']
run([GCC, *objects, res_o, *libs, '-mwindows', '-o', root / 'canity_verify.exe'])
write_profile(profile_path, dict(configuration=configuration, sources=source_hashes, resources=resources))
print('C99 game build: PASS', flush=True)
if args.build_only:
    raise SystemExit(0)

# ---- 离线自检：纯 C99，不碰 Win32 ----
run([GCC, *test_canity_sources(root), '-Iinclude', '-std=c99',
     '-Wall', '-Wextra', '-o', cache / 'test_canity.exe', '-lm'])
run([cache / 'test_canity.exe'])

# ---- 运行期集成：链接全部目标文件，控制台程序（不加 -mwindows）----
test_srcs = ['tests/test_runtime.c'] + sorted(
    str(q) for q in (root / 'tests' / 'domains').glob('*.c'))
run([GCC, *test_srcs, *objects, '-Iinclude', '-std=c99',
     '-Wall', '-Wextra', *libs, '-o', cache / 'test_runtime.exe'])
isolated = cache / 'test-data'
isolated.mkdir(exist_ok=True)
env = dict(os.environ, CANITY_DATA_DIR=str(isolated))
run([cache / 'test_runtime.exe'], env)

# ---- 分区守卫：系统头只允许出现在 include/common.h ----
run([sys.executable, '-X', 'utf8', 'tools/partition_check.py'])

print('ALL VALIDATION PASSED', flush=True)
