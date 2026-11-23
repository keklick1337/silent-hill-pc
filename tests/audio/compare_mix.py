#!/usr/bin/env python3
"""Compares two whole-session mixes of the game (SH_AUDIO_DUMP of two replay runs of one route
section 11). Both dumps run on the same VBlank timeline, so frame n of one is frame n of the other; no alignment.

  tests/audio/compare_mix.py A.wav B.wav [--window SECONDS] [--json OUT]

Per window and over the whole session (only windows where both are audible):
  envelope  Pearson correlation of the 10 ms loudness envelopes in dB
  spectrum  mean correlation of the log-magnitude spectra of 93 ms windows
  level     RMS of B minus RMS of A, dB
Needs numpy.
"""
import argparse
import json
import sys
import wave

import numpy as np


def load(path):
    with wave.open(path) as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 2:
            sys.exit('%s: 16-bit stereo PCM expected' % path)
        data = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').astype(np.float64)
        return w.getframerate(), data.reshape(-1, 2).mean(axis=1)


def envelope(x, hop):
    n = len(x) // hop
    return 20 * np.log10(np.sqrt(np.mean(x[:n * hop].reshape(n, hop) ** 2, axis=1)) + 1.0)


def spectra(x, size):
    n = len(x) // size
    if n == 0:
        return np.zeros((0, size // 2 + 1))
    win = np.hanning(size)
    return np.log10(np.abs(np.fft.rfft(x[:n * size].reshape(n, size) * win, axis=1)) + 1.0)


def metrics(a, b, rate):
    hop = rate // 100
    ea, eb = envelope(a, hop), envelope(b, hop)
    env = float(np.corrcoef(ea, eb)[0, 1]) if ea.std() > 0 and eb.std() > 0 else 1.0
    sa, sb = spectra(a, 4096), spectra(b, 4096)
    loud = (sa.mean(axis=1) > 0.5) & (sb.mean(axis=1) > 0.5)
    spec = [float(np.corrcoef(x, y)[0, 1]) for x, y in zip(sa[loud], sb[loud]) if x.std() > 0 and y.std() > 0]
    ra, rb = np.sqrt(np.mean(a ** 2)) + 1e-9, np.sqrt(np.mean(b ** 2)) + 1e-9
    return {'envelope': env, 'spectrum': float(np.mean(spec)) if spec else None,
            'level_db': float(20 * np.log10(rb / ra)), 'rms_a': float(ra), 'rms_b': float(rb)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--window', type=float, default=20.0)
    ap.add_argument('--json')
    args = ap.parse_args()
    rate, a = load(args.a)
    rate_b, b = load(args.b)
    if rate != rate_b:
        sys.exit('different rates')
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    step = int(args.window * rate)
    rows = []
    for start in range(0, n - step + 1, step):
        wa, wb = a[start:start + step], b[start:start + step]
        if np.sqrt(np.mean(wa ** 2)) < 30 or np.sqrt(np.mean(wb ** 2)) < 30:
            continue
        m = metrics(wa, wb, rate)
        m['from_s'] = start / rate
        rows.append(m)
        print('%7.1f s  envelope %.3f  spectrum %s  level %+.2f dB' % (
            m['from_s'], m['envelope'], '%.3f' % m['spectrum'] if m['spectrum'] is not None else '  -  ',
            m['level_db']))
    whole = metrics(a, b, rate)
    print('whole %.1f s: envelope %.3f  spectrum %s  level %+.2f dB  (%d audible windows)' % (
        n / rate, whole['envelope'], '%.3f' % whole['spectrum'] if whole['spectrum'] is not None else '-',
        whole['level_db'], len(rows)))
    if args.json:
        with open(args.json, 'w') as f:
            json.dump({'windows': rows, 'whole': whole}, f, indent=1)


if __name__ == '__main__':
    main()
