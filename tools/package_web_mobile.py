"""Extract the browser/touch distribution from the latest local playable package."""
from pathlib import Path
import hashlib
import json
import shutil
from zipfile import ZipFile, ZIP_DEFLATED

ROOT=Path(__file__).resolve().parents[1]

def main():
    package=Path((ROOT/'work/latest_playable_path.txt').read_text(encoding='utf-8').strip())
    source_info=json.loads((package/'build-info.json').read_text(encoding='utf-8'))
    notes=source_info.get('handoff','')
    if not notes or Path(notes).name!=notes:
        raise SystemExit('Latest package must declare a local handoff filename.')
    target=package.parent/(package.name+'_网页手机版')
    target.mkdir(exist_ok=False)
    for name in ('index.html','index.js','index.wasm','music','LICENSE','OFL-NotoSansSC.txt',notes,'标准化创作工作流.md'):
        src=package/name
        if src.is_dir(): shutil.copytree(src,target/name)
        elif src.is_file(): shutil.copy2(src,target/name)
    (target/'使用说明.txt').write_text('CANITY 2.4.2 RC 网页/手机触控版\n\n上传整个目录到支持WASM的静态网站。不要只上传HTML。\n本地电脑：在此目录启动 python -m http.server 8080，再访问 http://127.0.0.1:8080/ 。\n手机：与电脑同一网络，访问 http://电脑局域网地址:8080/；需允许网络连接。\n请横屏操作，首次点击后浏览器才允许播放音乐。\n本包是浏览器运行文件，不是Android/iOS安装程序。\n未执行浏览器或手机验收，由用户负责验证。\n',encoding='utf-8-sig')
    info=source_info
    manifest={k:info[k] for k in ('built_at','source','source_tree_sha256','save_version','game_version','map_size','container_slots','sprite_capacity','game_launched','acceptance_tests_run')}
    if info.get('acceptance_tests_run'):
        manifest['acceptance_verdict']=info.get('acceptance_verdict')
        (target/'使用说明.txt').write_text('CANITY 2.4.2 RC 网页/手机触控版\n\n整个目录部署到支持WASM的HTTP静态网站，需保留music资源。\n本地可在目录内运行 python -m http.server 8080，再访问 http://127.0.0.1:8080/ 。\n手机同一网络访问电脑局域网地址，推荐横屏。\n本轮已通过Chromium桌面及手机触控模拟；未作实体手机验收。\n全项目回归仍NO-GO，详见GAME验收报告。\n',encoding='utf-8-sig')
    manifest['platform']='web/mobile touch'
    manifest['assets_sha256']={p.relative_to(target).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(target.rglob('*')) if p.is_file()}
    (target/'build-info.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    archive=target.parent/(target.name+'.zip')
    with ZipFile(archive,'x',compression=ZIP_DEFLATED,compresslevel=6) as z:
        for p in sorted(target.rglob('*')):
            if p.is_file(): z.write(p,target.name+'/'+p.relative_to(target).as_posix())
    print(target)
    print(archive)

if __name__=='__main__': main()
