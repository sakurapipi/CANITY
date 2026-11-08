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

cache = root / 'work' / 'codex-build'
cache.mkdir(parents=True, exist_ok=True)

headers = list((root / 'include').glob('*'))
newest = max(p.stat().st_mtime for p in headers if p.is_file())


def run(cmd, env=None):
    p = subprocess.run([str(x) for x in cmd], env=env)
    if p.returncode:
        raise SystemExit(p.returncode)


print('toolchain: gcc=%s' % GCC, flush=True)
print('toolchain: windres=%s' % WINDRES, flush=True)

# ---- 资源：改了 rc 就要重编，否则版本信息/图标是旧的 ----
res_o = root / 'res' / 'canity_res.o'
rc = root / 'res' / 'canity.rc'
if not res_o.exists() or res_o.stat().st_mtime < rc.stat().st_mtime:
    run([WINDRES, rc, '-O', 'coff', '-o', res_o])
    print('resource: rebuilt canity_res.o', flush=True)

# ---- 逐个编译（增量）----
objects = []
for src in game_sources(root):
    obj = cache / (str(src.relative_to(root)).replace('\\', '_').replace('/', '_') + '.o')
    if not obj.exists() or obj.stat().st_mtime < max(src.stat().st_mtime, newest):
        run([GCC, '-c', src, '-Iinclude', '-std=c99', '-O2',
             '-Wall', '-Wextra', '-Werror', '-o', obj])
    objects.append(obj)

libs = ['-lgdi32', '-luser32', '-lwinmm']
run([GCC, *objects, res_o, *libs, '-mwindows', '-o', root / 'canity_verify.exe'])
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
