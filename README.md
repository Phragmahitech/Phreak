# Phreak — Phragma

Instrument VST3 (macOS universel, Windows 64 bits) : séquenceur glitch à partir d'un ou plusieurs samples.

- 4 samples (glisser-déposer), 16 points de départ posés sur la forme d'onde
- par point : sample, sens (FWD / REV / PING-PONG), stutter calé sur le tempo, pitch, gain, pan, filtre, envois reverb / delay, FX TAIL (KEEP / CUT)
- séquenceur 1 à 64 pas : GRID / CYCLE / RANDOM, calé sur le transport (SYNC) ou en temps libre (FREE TIME, DRIFT), swing, gate, chance
- hasard reproductible : SEED + LOCK ; REVERSE et SCATTER par pas ; RANDOMIZE des points (SNAP sur transitoires ou passage à zéro)
- MIDI : C1 > D#2 = points 1 > 16 ; autres notes = transposition (et lecture en mode NOTE)

Voir `INSTALLATION.txt`. Compilation : `cmake -S . -B build && cmake --build build --config Release` (JUCE 8.0.15 téléchargé automatiquement).
Moteur sans JUCE : `Source/PhreakEngine.h` ; tests : `-DPHREAK_BUILD_TESTS=ON`.
