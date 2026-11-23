#!/usr/bin/env python3
"""Compares a song rendered by the engine (sh-music, the importer's music converter) with the game's own mix
(SH_AUDIO_DUMP of a replay run).

  tests/audio/compare_music.py GAME.wav NATIVE.wav [--game-start SECONDS] [--seconds N] [--json OUT]

The game's mix is searched for the start of the song (the first sound after --game-start); both are cut to the same
length after aligning them on their loudness envelopes, then compared on:
  envelope  Pearson correlation of the 10 ms loudness envelopes in dB (timing, dynamics, note on/off)
  spectrum  mean correlation of the log-magnitude spectra of 93 ms windows (instruments, pitch, filtering)
  level     difference of the overall RMS in dB
  snr       signal-to-difference ratio of the waveforms after the best gain (only meaningful near sample accuracy)
Needs numpy.
"""
import argparse
import json
import sys
import wave

import numpy as np


def load(path):
    with wave.open(path) as w:
        if w.getsampwidth() != 2:
            sys.exit('%s: 16-bit PCM expected' % path)
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
        data = data.reshape(-1, w.getnchannels())
    return rate, data


def envelope(mono, hop):
    n = len(mono) // hop
    frames = mono[:n * hop].reshape(n, hop)
    rms = np.sqrt(np.mean(frames ** 2, axis=1)) + 1.0
    return 20 * np.log10(rms)


def first_sound(mono, start, threshold=64.0):
    idx = np.nonzero(np.abs(mono[start:]) > threshold)[0]
    return start + (int(idx[0]) if len(idx) else 0)


def spectra(mono, size=4096):
    n = len(mono) // size
    win = np.hanning(size)
    frames = mono[:n * size].reshape(n, size) * win
    mag = np.abs(np.fft.rfft(frames, axis=1))[:, 1:size // 4]  # up to 11 kHz
    return np.log10(mag + 1e-3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game')
    ap.add_argument('native')
    ap.add_argument('--game-start', type=float, default=0.0)
    ap.add_argument('--seconds', type=float, default=30.0)
    ap.add_argument('--json')
    args = ap.parse_args()

    rate, game = load(args.game)
    rate2, native = load(args.native)
    if rate != rate2:
        sys.exit('sample rates differ')
    gm = game.mean(axis=1)
    nm = native.mean(axis=1)
    hop = rate // 100

    g0 = first_sound(gm, int(args.game_start * rate))
    n0 = first_sound(nm, 0)
    length = int(args.seconds * rate)
    # Coarse alignment on the envelopes (+-2 s), then on the waveform (+-10 ms).
    ge = envelope(gm[g0:g0 + length + 4 * rate], hop)
    ne = envelope(nm[n0:n0 + length], hop)
    best, best_lag = -2.0, 0
    for lag in range(-200, 201):
        a = ge[max(0, lag):]
        b = ne[max(0, -lag):]
        m = min(len(a), len(b))
        if m < 100:
            continue
        c = np.corrcoef(a[:m], b[:m])[0, 1]
        if c > best:
            best, best_lag = c, lag
    g0 += best_lag * hop
    seg_n = nm[n0:n0 + length]
    fine, fine_lag = -2.0, 0
    for lag in range(-hop, hop + 1):
        seg_g = gm[g0 + lag:g0 + lag + len(seg_n)]
        if len(seg_g) < len(seg_n):
            continue
        c = np.dot(seg_g[:rate * 5], seg_n[:rate * 5])
        if c > fine:
            fine, fine_lag = c, lag
    g0 += fine_lag
    seg_g = gm[g0:g0 + len(seg_n)]
    m = min(len(seg_g), len(seg_n))
    seg_g, seg_n = seg_g[:m], seg_n[:m]

    env_corr = float(np.corrcoef(envelope(seg_g, hop), envelope(seg_n, hop))[0, 1])
    sg, sn = spectra(seg_g), spectra(seg_n)
    # Windows where either side is silent have no spectrum to compare.
    pairs = [(a, b) for a, b in zip(sg, sn) if np.std(a) > 0.05 and np.std(b) > 0.05]
    spec_corr = float(np.mean([np.corrcoef(a, b)[0, 1] for a, b in pairs])) if pairs else float('nan')
    rms_g = float(np.sqrt(np.mean(seg_g ** 2)))
    rms_n = float(np.sqrt(np.mean(seg_n ** 2)))
    gain = float(np.dot(seg_g, seg_n) / max(np.dot(seg_n, seg_n), 1.0))
    diff = seg_g - gain * seg_n
    snr = float(10 * np.log10(np.mean(seg_g ** 2) / max(np.mean(diff ** 2), 1e-9)))
    result = {
        'game_start_s': round(g0 / rate, 4), 'native_start_s': round(n0 / rate, 4), 'seconds': round(m / rate, 2),
        'envelope_corr': round(env_corr, 4), 'spectrum_corr': round(spec_corr, 4),
        'level_db': round(20 * np.log10(max(rms_n, 1e-9) / max(rms_g, 1e-9)), 2),
        'best_gain': round(gain, 4), 'snr_db': round(snr, 2),
    }
    print(json.dumps(result))
    if args.json:
        with open(args.json, 'w') as f:
            json.dump(result, f, indent=1)


if __name__ == '__main__':
    main()
