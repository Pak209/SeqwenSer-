# Engine notes

C++17, no allocation or locks on the audio thread. `core/capi/seqw_engine.h` is the C API used by Swift (and by any later AU wrapper).

- **Voices**: 8, each plays one slice of the loaded sample with its own pitch/reverse/lo-fi/stretch flags.
- **Sequencer**: sample-accurate, swing, rates 1/4 to 1/32 and triplet, play modes forward/reverse/ping-pong/random, two banks of 16 steps.
- **Step logic**: global probability x step probability, conditions (always, every 2nd/4th pass, first, not first, 50/50, velocity above/below 50).
- **Parameter locks**: each step may override any of the six effects (two normalised 0..1 values each), otherwise the global knob value is used.
- **Effects (normalised)**:
  | FX | A | B |
  |---|---|---|
  | PITCH | -12..+12 semitones | formant tilt (shelf) |
  | GRAIN | mix | scatter |
  | REPEAT | amount | rate 1/4 1/8 1/16 1/32 |
  | FILTER | low-pass cutoff 20 Hz..20 kHz (log) | resonance |
  | SPACE | reverb mix | size |
  | DRIVE | saturation | tone |
- **Slicing**: onset detection on a spectral-flux-like envelope with a sensitivity control; falls back to equal slices.
- **Sample hand-off**: the UI thread builds a new sample buffer and swaps it in with a hazard pointer; the old one is freed by `sq_collect_garbage` on the main thread.
- **Offline bounce**: `sq_bounce` runs the same code as the real-time path, so exports match what you hear.
