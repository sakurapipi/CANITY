"""Encode the authored WAV masters into the MP3 files shipped with the game.
No game/test process is started. WAV originals are retained for editing.
"""
from pathlib import Path
import concurrent.futures,hashlib,json,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1]
SCENES=('menu','explore','chase','safe','victory','defeat')
def encode(scene):
    src=ROOT/'music'/(scene+'.wav');dst=src.with_suffix('.mp3');temp=dst.with_name(scene+'.encoding.mp3')
    ffmpeg=shutil.which('ffmpeg')
    if not ffmpeg:raise SystemExit('Install ffmpeg to encode music; the game itself does not need it.')
    try:
        subprocess.run([ffmpeg,'-nostdin','-hide_banner','-loglevel','error','-y','-i',str(src),'-vn','-ar','44100','-ac','2','-c:a','libmp3lame','-b:a','128k',str(temp)],check=True)
        temp.replace(dst)
    finally:temp.unlink(missing_ok=True)
    return scene,src.stat().st_size,dst.stat().st_size,hashlib.sha256(dst.read_bytes()).hexdigest()
def main():
    results=list(concurrent.futures.ThreadPoolExecutor(max_workers=2).map(encode,SCENES))
    p=ROOT/'music/score-manifest.json';manifest=json.loads(p.read_text('utf-8'));manifest['distribution_format']='mp3 / 44100 Hz stereo / 128 kbps'
    for scene,original,size,sha in results:
        manifest['tracks'][scene].update(mp3_sha256=sha,mp3_bytes=size)
    p.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n','utf-8')
    print('Music encoded: WAV %.1f MB -> MP3 %.1f MB'%(sum(r[1] for r in results)/1048576,sum(r[2] for r in results)/1048576))
if __name__=='__main__':main()
