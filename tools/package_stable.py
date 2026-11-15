"""Package stable build artifacts without running tests, starting the game or pushing."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,default=Path.home()/'Desktop'/'GAME1')
    ap.add_argument('--validation-report',type=Path)
    ap.add_argument('--source-commit')
    args=ap.parse_args();report=None
    if args.validation_report:
        report=json.loads(args.validation_report.read_text('utf-8'))
        if not report.get('passed') or report.get('build_only'):raise SystemExit('Full validation report must pass')
        from build_cache import validation_digest
        if report.get('validation_inputs_sha256')!=validation_digest(ROOT):raise SystemExit('Validation inputs changed')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    binary=ROOT/'canity_verify.exe';dist=ROOT/'web/dist'
    for p in [binary,dist/'index.html',dist/'index.js',dist/'index.wasm',dist/'standalone.html']:
        if not p.is_file():raise SystemExit('Build artifact missing: '+str(p))
    name='CANITY_2.4.2.exe';exe=out/name;shutil.copy2(binary,exe)
    gcc=Path(os.environ.get('CANITY_GCC','gcc'))
    strip=gcc.parent/'strip.exe'
    if strip.is_file():subprocess.run([str(strip),'--strip-unneeded',str(exe)],check=True)
    legal=out/'资料';legal.mkdir(exist_ok=True)
    for source in [ROOT/'LICENSE',ROOT/'l10n/OFL-NotoSansSC.txt',ROOT/'third_party/minimp3/minimp3-LICENSE.txt',ROOT/'docs/RELEASE-v2.4.2.md']:
        if source.is_file():shutil.copy2(source,legal/source.name)
    if report:shutil.copy2(args.validation_report,legal/'验收摘要.json')
    web=out/'Web';web.mkdir(exist_ok=True)
    for name2 in ['index.html','index.js','index.wasm']:shutil.copy2(dist/name2,web/name2)
    shutil.copytree(dist/'music',web/'music',dirs_exist_ok=True)
    shutil.copy2(ROOT/'LICENSE',web/'LICENSE');shutil.copy2(ROOT/'l10n/OFL-NotoSansSC.txt',web/'OFL-NotoSansSC.txt')
    (web/'使用说明.txt').write_text('在此目录运行 python -m http.server 8080，通过HTTP访问。手机横屏；允许首次点击启用声音。不支持原生全屏的浏览器可窗口游玩。实体手机兼容性仍取决于浏览器；自动化模拟结果见验收报告。','utf-8-sig')
    def archive(path,entries):
        with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for p,rel in entries:z.write(p,rel)
    archive(out/'CANITY_2.4.2_Windows.zip',[(exe,exe.name)]+[(p,'资料/'+p.name) for p in legal.iterdir() if p.is_file()])
    archive(out/'CANITY_2.4.2_网页手机版.zip',[(p,'Web/'+p.relative_to(web).as_posix()) for p in web.rglob('*') if p.is_file()])
    archive(out/'CANITY_2.4.2_单文件网页.zip',[(dist/'standalone.html','CANITY_2.4.2.html')]+[(p,'资料/'+p.name) for p in legal.iterdir() if p.is_file()])
    # Source archives contain exactly version-controlled inputs, never local scratch files.
    tracked=subprocess.run(['git','ls-files','-z'],cwd=ROOT,stdout=subprocess.PIPE,check=True).stdout.decode('utf-8').split('\0')
    sources=[ROOT/name for name in tracked if name and (ROOT/name).is_file()]
    archive(out/'CANITY_2.4.2_源码.zip',[(p,'Source/'+p.relative_to(ROOT).as_posix()) for p in sources])
    manifest={'version':'2.4.2','save_version':45,'map_size':[36,36],'container_slots':12,'windows_compiled':True,'web_compiled':True,'tests_run':bool(report),'source_git_commit':args.source_commit,'manual_acceptance':False,'pushed':False,'native_launch':'direct EXE; MP3 resources embedded; no BAT/DAT dependency','native_music_cache':'%LOCALAPPDATA%/CANITY/audio-2.4.2','default_save_directory':'%LOCALAPPDATA%/CANITY/saves; CANITY_DATA_DIR respected','files':{}}
    for p in [exe,*sorted(out.glob('CANITY_2.4.2_*.zip'))]:manifest['files'][p.name]={'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
    (out/'交付清单.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf-8')
    (out/'先看这里.txt').write_text('CANITY 2.4.2\n\n直接双击 CANITY_2.4.2.exe 运行Windows版，不需要BAT或DAT。音乐嵌在EXE内，运行时缓存到用户目录。\n默认存档：%LOCALAPPDATA%/CANITY/saves，原GAME存档没有修改。若要继续旧进度，请先备份再迁入，或设置CANITY_DATA_DIR。\nWeb为网页/手机触控版，通过HTTP访问。单文件网页在单独ZIP中。\n本轮验证结果见交付清单、验收报告和GitHub实际记录，不能将编译成功视为人工验收。Source是本轮可编辑源码。\n','utf-8-sig')
    print(json.dumps({k:v['bytes'] for k,v in manifest['files'].items()},ensure_ascii=False))

if __name__=='__main__':main()
