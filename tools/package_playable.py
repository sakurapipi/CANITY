"""Package an already compiled local game; does not run it, test it or push git.

Default package is Lite: copy canity.exe + external music/ (MP3, chart, manifest).
For a Full single EXE, first build with build_validate.py --embed-music, then
pass --single-exe --embed-music.

python tools/build_validate.py --build-only
python tools/build_web.py                 # optional browser build
python tools/package_playable.py --include-web
"""
from pathlib import Path
from datetime import datetime
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

from build_sources import game_sources
from build_cache import file_digest, include_digest, validation_digest

ROOT = Path(__file__).resolve().parents[1]
SCENES = ('menu', 'explore', 'chase', 'safe', 'victory', 'defeat')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path.home()/'Desktop'/'GAME')
    parser.add_argument('--include-web', action='store_true')
    parser.add_argument('--single-exe', action='store_true',
                        help='Only for a Full build made with build_validate.py --embed-music')
    parser.add_argument('--embed-music', action='store_true',
                        help='Assert that the compiled EXE already embeds MP3 resources (Full build)')
    parser.add_argument('--label', default='CANITY_2.4.6_完整运行包')
    parser.add_argument('--notes', type=Path, default=Path('docs/RELEASE-v2.4.6.md'))
    parser.add_argument("--acceptance-report", type=Path)
    parser.add_argument("--source-commit", default=os.environ.get("GITHUB_SHA"))
    args = parser.parse_args()
    if args.single_exe and not args.embed_music:
        raise SystemExit('默认是 Lite 构建：EXE 不含 MP3，必须保留 music/ 目录。'
                         '只有用 build_validate.py --embed-music 重建后才可使用 --single-exe --embed-music。')
    if args.source_commit and not re.fullmatch(r"[0-9a-fA-F]{40}",args.source_commit):
        raise SystemExit("source commit must be a complete Git SHA")
    acceptance = json.loads(args.acceptance_report.read_text(encoding="utf-8")) if args.acceptance_report else None
    if acceptance and acceptance.get('validation_inputs_sha256'):
        if acceptance['validation_inputs_sha256'] != validation_digest(ROOT):
            raise SystemExit('验证报告与当前源码、测试或工作流不匹配，请重新验证。')
        if acceptance.get('build_only') or not acceptance.get('passed'):
            raise SystemExit('不能把仅编译或失败报告作为通过验收证据。')
        acceptance['verdict']='AUTOMATED_PASS; desktop acceptance not implied'
    binary = ROOT/'canity_verify.exe'
    profile_file = ROOT/'work/codex-build/compile-profile.json'
    if not binary.is_file() or not profile_file.is_file():
        raise SystemExit('先运行 build_validate.py --build-only，生成当前运行程序。')
    profile = json.loads(profile_file.read_text(encoding='utf-8'))
    if profile.get('configuration', {}).get('includes') != include_digest(ROOT):
        raise SystemExit('头文件已改变，请先重新编译再打包。')
    for source in game_sources(ROOT):
        key = source.relative_to(ROOT).as_posix()
        if profile.get('sources', {}).get(key) != file_digest(source):
            raise SystemExit('源码已改变，请先重新编译再打包：'+key)
    needed = [ROOT/'music'/(scene+suffix)
              for scene in SCENES for suffix in ('.mp3', '.chart.json')]
    needed += [ROOT/'music/score-manifest.json']
    for path in needed:
        if not path.is_file():
            raise SystemExit('缺少运行资源，请先生成配乐并运行 tools/encode_music_mp3.py：'+str(path))
    web_files = [ROOT/'web/dist'/name for name in ('index.html', 'index.js', 'index.wasm')]
    if args.include_web and not all(path.is_file() for path in web_files):
        raise SystemExit('缺少网页构建，请先运行 build_web.py。')
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    if any(c in args.label for c in ('/', '\\', ':')) or args.label in ('.', '..'):
        raise SystemExit('运行包名称不能包含路径。')
    folder = args.output/(args.label+'_'+stamp)
    folder.mkdir(parents=True, exist_ok=False)
    shutil.copy2(binary, folder/'canity.exe')
    music = folder/'music'
    if not args.single_exe:
        music.mkdir()
        for path in needed:
            shutil.copy2(path, music/path.name)
    if args.include_web:
        for path in web_files:
            shutil.copy2(path, folder/path.name)
    for relative in ('LICENSE', 'l10n/OFL-NotoSansSC.txt', str(args.notes), 'docs/标准化创作工作流.md', 'third_party/minimp3/minimp3-LICENSE.txt'):
        source = ROOT/relative
        if source.is_file():
            shutil.copy2(source, folder/source.name)
    compiler = os.environ.get('CANITY_GCC') or profile.get('configuration', {}).get('compiler', {}).get('path') or shutil.which('gcc')
    runtime = []
    if compiler:
        compiler_bin = Path(compiler).resolve().parent
        objdump = compiler_bin/'objdump.exe'
        if objdump.is_file():
            result = subprocess.run([str(objdump), '-p', str(binary)], capture_output=True,
                                    text=True, encoding='utf-8', errors='replace', check=True)
            pending = re.findall(r'DLL Name:\s*(\S+)', result.stdout)
            seen = set()
            while pending:
                name = pending.pop()
                if name.lower() in seen:
                    continue
                seen.add(name.lower())
                dll = compiler_bin/name
                if not dll.is_file():
                    continue  # Windows system DLLs are supplied by Windows.
                shutil.copy2(dll, folder/name)
                runtime.append(name)
                result = subprocess.run([str(objdump), '-p', str(dll)], capture_output=True,
                                        text=True, encoding='utf-8', errors='replace', check=True)
                pending.extend(re.findall(r'DLL Name:\s*(\S+)', result.stdout))
    launcher = ('@echo off\r\nsetlocal\r\ncd /d "%~dp0"\r\n'
                'if not exist "saves" mkdir "saves"\r\n'
                'if not defined CANITY_DATA_DIR set "CANITY_DATA_DIR=%~dp0saves"\r\n'
                'if not exist "%CANITY_DATA_DIR%" mkdir "%CANITY_DATA_DIR%"\r\n'
                'if not defined CANITY_MUSIC set "CANITY_MUSIC=files"\r\n'
                'start "" /wait "%~dp0canity.exe"\r\nendlocal\r\n')
    if not args.single_exe:(folder/'开始游戏.bat').write_bytes(launcher.encode('ascii'))
    version_header = (ROOT/'include/common.h').read_text(encoding='utf-8-sig')
    game_version = re.search(r'^#define\s+CANITY_VERSION_STR\s+"([^"]+)"', version_header, re.M)[1]
    save_header = (ROOT/'include/save.h').read_text(encoding='utf-8-sig')
    save_version = int(re.search(r'^#define\s+SAVE_VERSION\s+(\d+)', save_header, re.M)[1])
    if acceptance:
        shutil.copy2(args.acceptance_report,folder/'验收摘要.json')
    if args.single_exe:
        strip=Path(compiler).resolve().parent/'strip.exe' if compiler else None
        if strip and strip.is_file():subprocess.run([str(strip),'--strip-unneeded',str(folder/'canity.exe')],check=True)
    description = ('音乐已嵌入 EXE，首次运行缓存到用户目录。' if args.single_exe else
                   '默认 Lite 构建，必须保留 music/ 目录和随包 DLL。')
    instructions = (f'CANITY {game_version} — 存档格式 v{save_version}\n\n'
                    '完整解压后双击 canity.exe，无需 BAT 或 DAT。'+description+'\n'
                    '直接运行默认使用 %LOCALAPPDATA%/CANITY/saves；尊重 CANITY_DATA_DIR。\n'
                    '可选的“开始游戏.bat”使用包内 saves/，与直接运行的默认位置不同。\n'
                    '升级前请备份自己的存档，不要用测试目录覆盖玩家数据。\n\n'
                    '网页及手机：通过 HTTP 打开 index.html，保留 index.js、index.wasm 和 music/。\n'
                    '手机推荐横屏；此包不是 APK。详细变更见随包修改说明。\n')
    instructions += ('本轮自动化结果见验收摘要.json；不代表 Steam 双设备或实体手机实测。\n' if acceptance else
                     '此包未附本轮验证报告，不能据此宣称通过验收。\n')
    (folder/'运行说明.txt').write_text(instructions,encoding='utf-8-sig')
    source_hashes = {}
    for directory in ('src', 'include', 'tools', 'tests', 'l10n', 'third_party'):
        for path in sorted((ROOT/directory).rglob('*')):
            if path.is_file() and path.suffix in ('.c', '.h', '.inc', '.py', '.tsv', '.json'):
                source_hashes[path.relative_to(ROOT).as_posix()] = file_digest(path)
    canonical = json.dumps(source_hashes, sort_keys=True, separators=(',', ':')).encode()
    save_header = (ROOT/'include/save.h').read_text(encoding='utf-8-sig')
    version_header = (ROOT/'include/common.h').read_text(encoding='utf-8-sig')
    save_version = int(re.search(r'^#define\s+SAVE_VERSION\s+(\d+)', save_header, re.M)[1])
    game_version = re.search(r'^#define\s+CANITY_VERSION_STR\s+"([^"]+)"', version_header, re.M)[1]
    manifest = {'built_at':datetime.now().isoformat(timespec='seconds'),
                'source':('commit '+args.source_commit) if args.source_commit else 'local working tree; no commit attached',
                'source_git_commit':args.source_commit, 'save_version':save_version,
                'game_version':game_version, 'handoff':args.notes.name,
                'map_size':[36,36], 'container_slots':12, 'sprite_capacity':64,
                'source_tree_sha256':hashlib.sha256(canonical).hexdigest(),
                'exe_sha256':file_digest(folder/'canity.exe'), 'runtime_dlls':runtime,
                'windows_compiled':True, 'web_included':args.include_web,
                'web_sha256':{p.name:file_digest(p) for p in web_files} if args.include_web else {},
                'game_launched':bool(acceptance and acceptance.get('native_window_run')), 'acceptance_tests_run':bool(acceptance),
                'acceptance_verdict':acceptance.get('verdict') if acceptance else 'not_run',
                'acceptance_report_sha256':file_digest(args.acceptance_report) if acceptance else None,
                'files':source_hashes}
    (folder/'build-info.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n',encoding='utf-8')
    # A version label contains dots; replacing its "suffix" truncates the name.
    archive = folder.parent/(folder.name+'.zip')
    with ZipFile(archive, 'x', compression=ZIP_DEFLATED, compresslevel=6) as output:
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                output.write(path, folder.name+'/'+path.relative_to(folder).as_posix())
    latest = ROOT/'work/latest_playable_path.txt'
    latest.parent.mkdir(parents=True, exist_ok=True)
    latest.write_text(str(folder), encoding='utf-8')
    print(folder)
    print(archive)
    print('ZIP %.1f MB'%(archive.stat().st_size/1024**2))


if __name__ == '__main__':
    main()
