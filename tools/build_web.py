"""CANITY 网页版构建 —— 用 Emscripten 把同一份 C99 源码编成 WASM。

用法：
    python -X utf8 tools/build_web.py            # 发布构建（-O2）
    python -X utf8 tools/build_web.py --debug    # 快速构建（-O0，带断言）

工具链：
    EM_CONFIG 指向 emsdk 的配置；未设时用本机默认路径（见 EM_CONFIG_DEFAULT）。
    真正的编译器是 upstream/emscripten/emcc.py，用当前 python 启动。

产出（web/dist/）：
    index.html   ← 由 web/shell.html 套壳
    index.js     ← Emscripten 胶水
    index.wasm   ← 游戏本体
"""
from pathlib import Path
import os
import base64
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_sources import game_sources  # noqa: E402

# =====================================================================
# 工具链定位（本地 / CI 都走这里）
# ---------------------------------------------------------------------
# 2026-11-06：CI 上 Pages 工作流失败 —— build_web.py 硬写了
#   ~/.dsh/tools/emsdk/...，而 GitHub Actions 里 emsdk 是
#   mymindstorm/setup-emsdk 装的，路径完全不同（emcc 在 PATH 上）。
#
# 优先级：
#   ① CANITY_EMSDK 环境变量（本地覆盖）
#   ② EMSDK 环境变量（CI 的 setup-emsdk 会设）
#   ③ ~/.dsh/tools/emsdk（本机默认）
#   ④ 直接找 PATH 上的 emcc
# =====================================================================
def _resolve_emcc():
    cands = []
    if os.environ.get('CANITY_EMSDK'):
        cands.append(Path(os.environ['CANITY_EMSDK']))
    if os.environ.get('EMSDK'):
        cands.append(Path(os.environ['EMSDK']))
    cands.append(Path.home() / '.dsh' / 'tools' / 'emsdk')

    for home in cands:
        py = home / 'upstream' / 'emscripten' / 'emcc.py'
        if py.is_file():
            cfg = home / 'upstream' / 'emscripten' / '.emscripten'
            return ([sys.executable, '-X', 'utf8', str(py)], cfg if cfg.is_file() else None)

    exe = shutil.which('emcc')
    if exe:
        return ([exe], None)
    return (None, None)


EMCC_CMD, EM_CONFIG_DEFAULT = _resolve_emcc()
EMCC = Path(EMCC_CMD[-1]) if EMCC_CMD else None


DIST = ROOT / 'web' / 'dist'
SHELL = ROOT / 'web' / 'shell.html'


def main(argv):
    debug = '--debug' in argv
    single = '--single' in argv

    if not EMCC_CMD:
        print('ERROR: 找不到 emcc（试过 CANITY_EMSDK / EMSDK / ~/.dsh/tools/emsdk / PATH）', file=sys.stderr)
        print()
        print('装法（一次性）：')
        print('  git clone --depth 1 https://github.com/emscripten-core/emsdk.git %s' % EMSDK_HOME)
        print('  cd %s && emsdk.bat install latest' % EMSDK_HOME)
        print()
        print('注意：emsdk 的 unzip 在 Windows 上可能卡在 test/ 目录，')
        print('      失败时可以用 python zipfile 手动解压 install/ 到 upstream/。')
        return 2

    env = dict(os.environ)
    if 'EM_CONFIG' not in env and EM_CONFIG_DEFAULT and EM_CONFIG_DEFAULT.is_file():
        env['EM_CONFIG'] = str(EM_CONFIG_DEFAULT)

    srcs = [str(p) for p in game_sources(ROOT, platform='web')]
    if not srcs:
        print('ERROR: 没有收集到任何源文件', file=sys.stderr)
        return 2

    DIST.mkdir(parents=True, exist_ok=True)

    cmd = [
        *EMCC_CMD,
        *srcs,
        '-Iinclude',
        '-std=c99',
        '-Werror=implicit-function-declaration',
        '-Werror=incompatible-pointer-types',
        '-DCANITY_WEB=1',
        '-O0' if debug else '-O3',
        '-sWASM=1',
        '-sALLOW_MEMORY_GROWTH=1', '-sMAXIMUM_MEMORY=536870912', '-sMALLOC=emmalloc',
        '-sINITIAL_MEMORY=67108864',
        '-sSTACK_SIZE=1048576',
        '-sASSERTIONS=%d' % (1 if debug else 0),
        '-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,HEAP32,UTF8ToString,stringToUTF8',
        '-sEXPORTED_FUNCTIONS=_CanityWebInputState,_CanityWebPointer,_CanityWebZoom,_CanityUiFlags,_CanityUiAccent,_HelpBookText,_main,_malloc,_free,_CanityProbeWorldSound,_CanityProbePanner,_CanityActionVk,_CanityActionMenu,_CanityActionPause,_CanityActionFlashlight,_CanityProbeFlashlight,_CanityProbeState,_CanityProbeMenu,_CanityProbeKeys,_CanityProbeUpdates,_CanityProbeDt,_CanityProbeGameKey,_CanityProbeDir,_CanityProbeLayout',
        '-sINCOMING_MODULE_JS_API=canvas,print,printErr,setStatus,onRuntimeInitialized,locateFile',
        '--shell-file', str(SHELL),
    ]
    if single:
        # 2026-11-06：**单文件版** —— wasm 以 base64 内联进 HTML，
        # 双击就能玩，不需要 HTTP 服务（file:// 下 fetch 会被拦）。
        cmd += ['-sSINGLE_FILE=1', '-o', str(DIST / 'standalone.html')]
    else:
        cmd += ['-o', str(DIST / 'index.html')]
    if debug:
        cmd += ['-g', '-sASSERTIONS=2']

    print('sources: %d 个 .c（已排除桌面后端与 Win32 音频）' % len(srcs))
    print('emcc   : %s' % ' '.join(EMCC_CMD))
    print('config : %s' % env.get('EM_CONFIG', '(默认)'))
    print('output : %s' % (DIST / 'index.html'))
    print(flush=True)

    p = subprocess.run(cmd, env=env)
    if p.returncode:
        print('构建失败（退出码 %d）' % p.returncode, file=sys.stderr)
        return p.returncode

    if single:
        tracks={}
        for i,name in enumerate(('menu','explore','chase','safe','victory','defeat')):
            mp3=ROOT/'music'/(name+'.mp3')
            if not mp3.is_file():raise SystemExit('Missing standalone music: '+str(mp3))
            tracks[i]='data:audio/mpeg;base64,'+base64.b64encode(mp3.read_bytes()).decode('ascii')
        html=DIST/'standalone.html'
        content=html.read_text(encoding='utf-8')
        content=content.replace('<script','<script>globalThis.CANITY_MUSIC_TRACKS='+json.dumps(tracks,separators=(',',':'))+';</script><script',1)
        html.write_text(content,encoding='utf-8')

    if (ROOT / 'music').is_dir():
        shutil.copytree(ROOT / 'music', DIST / 'music', dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('*.wav'))
        # Remove only superseded generated distribution audio, not authoring masters.
        for old_wave in (DIST / 'music').glob('*.wav'):
            if old_wave.with_suffix('.mp3').is_file(): old_wave.unlink()

    # Emscripten 会把 shell 里 {{{ SCRIPT }}} 之外的东西保留，但下载链接是写死的，
    # 这里做一次产出清点，方便直接看结果。
    total = 0
    for f in sorted(DIST.iterdir()):
        if f.is_file():
            total += f.stat().st_size
            print('  %-16s %8.1f KB' % (f.name, f.stat().st_size / 1024.0))
    print('合计 %.1f KB' % (total / 1024.0))
    print()
    if single:
        print('单文件版：%s' % (DIST / 'standalone.html'))
        print('  直接双击打开即可（内联了 wasm，不需要服务器）')
    else:
        print('本地预览：')
        print('  cd %s && python -m http.server 8080' % DIST)
        print('  然后打开 http://127.0.0.1:8080/')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
