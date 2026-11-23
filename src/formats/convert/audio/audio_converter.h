#pragma once

#include "formats/convert/converter.h"

#include <memory>

namespace sh::convert::audio {

// "vab": a VAB bank (header, program and tone tables, SPU-ADPCM samples) becomes audio/vab/<bank>/<NNN>.wav,
// one mono 16-bit WAV per sample (NNN is the 1-based sample number the tones refer to; loops go to the smpl chunk),
// and audio/vab/<bank>.vab.json with the programs, every tone slot of their tables, ADSR, root keys, pans and volumes.
// Not lossless (the ADPCM is decoded), but it is what the sound engine plays (engine/audio/sound_bank.h).
std::unique_ptr<Converter> make_vab_converter();

// "xa": a track of raw 2336-byte XA sectors (the files under XA/ of HILL.) becomes one 16-bit WAV per (file, channel)
// stream of ADPCM audio sectors, audio/xa/<track>[_fNN_chN].wav, and audio/xa/<track>.xa.json with the sector map
// (sector_runs). Not lossless, but it is what the sound engine plays voice lines from (engine/audio/engine.h).
std::unique_ptr<Converter> make_xa_converter();

// "kdt": a KDT1 music sequence (SND/*.KDT) becomes audio/seq/<path>.kdt.json (every event, lossless: encode() gives
// the file back) and audio/seq/<path>.mid (Standard MIDI, format 1, for listening and editing; the instruments are
// the programs of the VAB of the same name). Format notes in kdt_converter.cpp.
std::unique_ptr<Converter> make_kdt_converter();

}  // namespace sh::convert::audio
