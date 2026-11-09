"""Render CANITY's original, deterministic score and PCM-derived rhythm charts.

Asset authoring, not a game/test runner. Requires numpy; no network/assets.
All oscillators are periodic over the complete track. Percussion/reverb wraps
around the loop instead of adding a fade-out tail. Output is PCM16/44100/stereo.
"""
from pathlib import Path
import argparse
import hashlib
import json
import wave

import numpy as np

RATE = 44100
ROOT = Path(__file__).resolve().parents[1]
SCENES = ("menu", "explore", "chase", "safe", "victory", "defeat")
# Integral bar counts keep the drone, polyrhythm and reverb on one loop period.
SCORES = {
    "menu": (72, 24, 36.0, 913),
    "explore": (80, 24, 32.0, 1223),
    "chase": (120, 40, 39.0, 2221),
    "safe": (64, 20, 30.0, 3119),
    "victory": (80, 24, 40.0, 4229),
    "defeat": (60, 20, 28.0, 5333),
}


def put(out, at, sound, gain=1.0, pan=0.0):
    """Circular placement preserves releases that cross the loop boundary."""
    indices = (int(round(at * RATE)) + np.arange(len(sound))) % len(out)
    out[indices, 0] += sound * gain * np.sqrt((1 - pan) * .5)
    out[indices, 1] += sound * gain * np.sqrt((1 + pan) * .5)


def hit(rng, kind, duration=.65, pitch=55.0):
    t = np.arange(int(RATE * duration), dtype=np.float64) / RATE
    attack = np.minimum(t / .006, 1)
    end = np.minimum((duration - t) / .03, 1)
    noise = rng.uniform(-1., 1., len(t))
    if kind == "pulse":
        sound = np.sin(2 * np.pi * (pitch * t + 18 * (1 - np.exp(-t * 18))))
        sound *= np.exp(-t * 10)
    elif kind == "metal":
        sound = sum(np.sin(2*np.pi*pitch*f*t) for f in (1, 1.4142, 2.731, 4.117)) / 4
        sound = (sound + noise * .17) * np.exp(-t * 4)
    elif kind == "scrape":
        sound = (noise - np.roll(noise, 1)) * .23
        sound += np.sin(2*np.pi*(1600*t + 450*t*t)) * .14
        sound *= np.exp(-t * 2.5)
    else:
        sound = (np.sin(2*np.pi*34*t) + noise * .08) * np.exp(-t * 5)
    return (sound * attack * end).astype(np.float32)


def compose(name):
    bpm, bars, fundamental, seed = SCORES[name]
    beat = 60 / bpm
    duration = bars * 4 * beat
    n = int(round(duration * RATE))
    rng = np.random.default_rng(seed)
    t = np.arange(n, dtype=np.float64) / RATE
    score = np.zeros((n, 2), dtype=np.float32)
    # No major/minor accompaniment: detuned octaves, minor seconds, tritones.
    for freq, gain, pan in ((fundamental,.19,-.25), (fundamental*1.012,.085,.35),
                            (fundamental*2,.055,.1), (fundamental*2.1189,.028,-.6)):
        cycles = round(freq * duration)
        phase = 2*np.pi*cycles*t/duration
        breath = .72 + .28*np.sin(2*np.pi*(3 if name=="safe" else 7)*t/duration)
        tone = np.sin(phase + .2*np.sin(2*np.pi*5*t/duration))*breath*gain
        score[:,0] += tone * np.sqrt((1-pan)*.5)
        score[:,1] += tone * np.sqrt((1+pan)*.5)
    # Beat accents remain readable for calibration without becoming dance music.
    step = .5 if name == "chase" else 1.
    for index, at in enumerate(np.arange(0, duration, beat * step)):
        bar = int(at / (4*beat))
        # Three deliberately quiet bars immediately precede three different shocks.
        if bar in (5, 11, 17):
            continue
        if name == "safe" and index % 2:
            continue
        kind = "pulse" if index % 4 != 3 else "metal"
        gain = .22 if name == "chase" else .13
        put(score, at, hit(rng, kind, .45, fundamental*1.5), gain,
            (-.35,.25,.1,-.2)[index % 4])
        if index % (6 if name == "chase" else 11) == 4:
            put(score, at + beat*.37, hit(rng,"metal",1.2,373+index*3),.08,.65)
    # Quiet -> metal slash / sub impact / glass-like dissonant attack.
    for bar, kind, pitch in ((6,"scrape",1700), (12,"impact",34), (18,"metal",2381)):
        if bar >= bars:
            continue
        start = int((bar-1)*4*beat*RATE)
        stop = int(bar*4*beat*RATE)
        u = np.linspace(0, 1, stop-start)
        score[start:stop] *= (.025 + .975*(1-u)**4)[:,None]
        put(score, bar*4*beat+.137, hit(rng,kind,1.3,pitch),.65,
            -.65 if bar == 6 else .55)
    # Circular early reflections: no callback allocation and no external SFX.
    dry = score.copy()
    for delay, gain in ((.071,.16),(.139,.10),(.263,.07),(.431,.04)):
        score += np.roll(dry, int(delay*RATE), axis=0)[:,::-1] * gain
    score -= score.mean(axis=0)
    # Soft saturation sets crest factor, then target RMS -17 dBFS, ceiling -1 dBFS.
    score = np.tanh(score * 2.0)
    rms = float(np.sqrt(np.mean(score.astype(np.float64)**2)))
    score *= (10**(-17/20)) / max(rms, 1.e-12)
    ceiling = 10**(-1/20)
    peak = float(np.max(np.abs(score)))
    if peak > ceiling:
        score *= ceiling / peak
    pcm = np.rint(score * 32767).astype('<i2')
    return pcm, bpm, duration


def chart_from_pcm(pcm):
    """10ms energy-flux peaks from the middle 40s, 240ms refractory interval.

    Derive lanes from the PCM digest and onset ordinal, not Python's hash/RNG.
    A note at time_ms is relative to the clip's exact start_sample. Notes are
    sparse enough for novice QWER input; higher floors include more onsets.
    """
    clip_start = (len(pcm) - RATE * 40) // 2
    clip_start = max(RATE*8, clip_start)
    window = RATE // 100
    mono = pcm.astype(np.float32).mean(axis=1) / 32768.
    clip = mono[clip_start:clip_start+RATE*40]
    count = len(clip)//window
    energy = np.sqrt(np.mean(clip[:count*window].reshape(count,window)**2,axis=1))
    flux = np.maximum(0, energy - np.roll(energy,2))
    threshold = max(.006, float(np.quantile(flux, .78)))
    candidates = [i for i in range(30,count-30)
                  if flux[i] > threshold and flux[i] >= max(flux[i-2:i+3])]
    selected = []
    for i in candidates:
        if selected and i - selected[-1] < 24:
            if flux[i] > flux[selected[-1]]:
                selected[-1] = i
        else:
            selected.append(i)
    digest = hashlib.sha256(pcm.tobytes()).digest()
    notes = [{"time_ms":i*10,"lane":(digest[j % 32]+j)%4,
              "difficulty":1+(j%3)} for j,i in enumerate(selected)]
    return {"format":"canity-onset-v1", "rate":RATE,
            "start_sample":clip_start, "duration_ms":40000,
            "pcm_sha256":digest.hex(), "notes":notes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'music')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"composer":"CANITY original procedural score", "version":1,
                "algorithm":"tools/compose_score.py", "tracks":{}}
    header = ['/* Generated by tools/compose_score.py. Do not edit. */',
              '#ifndef CANITY_SCORE_CHARTS_H', '#define CANITY_SCORE_CHARTS_H',
              'typedef struct { unsigned short ms; unsigned char lane, difficulty; } ScoreNote;',
              'typedef struct { int startSample, count; const ScoreNote* notes; } ScoreChart;']
    entries=[]
    for name in SCENES:
        pcm,bpm,duration=compose(name)
        path=args.output/(name+'.wav')
        with wave.open(str(path),'wb') as output:
            output.setnchannels(2); output.setsampwidth(2); output.setframerate(RATE)
            output.writeframes(pcm.tobytes())
        chart=chart_from_pcm(pcm)
        (args.output/(name+'.chart.json')).write_text(
            json.dumps(chart,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        header.append('static const ScoreNote score_'+name+'[] = {')
        for note in chart['notes']:
            header.append('    {%d,%d,%d},'%(note['time_ms'],note['lane'],note['difficulty']))
        header.append('};')
        entries.append('    {%d,sizeof(score_%s)/sizeof(score_%s[0]),score_%s},'%(
            chart['start_sample'],name,name,name))
        rms=float(np.sqrt(np.mean((pcm.astype(np.float64)/32768.)**2)))
        manifest['tracks'][name]={"bpm":bpm,"seconds":duration,
            "wav_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
            "rms_dbfs":round(20*np.log10(rms),3),
            "peak_dbfs":round(20*np.log10(np.max(np.abs(pcm.astype(np.float64)))/32768.),3),
            "notes":len(chart['notes'])}
        print('Rendered '+str(path))
    header += ['static const ScoreChart scoreCharts[6] = {',*entries,'};','#endif','']
    (ROOT/'include/score_charts.h').write_text('\n'.join(header),encoding='utf-8')
    (args.output/'score-manifest.json').write_text(
        json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
