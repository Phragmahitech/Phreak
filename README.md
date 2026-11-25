# Phreak — Phragma

Instrument VST3 (macOS universel, Windows 64 bits) : séquenceur glitch à partir d'un ou plusieurs samples.
Une note MIDI longue = une séquence sans fin, calée sur la grille du morceau.

- 4 samples (glisser-déposer), 16 points de départ posés sur la forme d'onde
- par point : sample, sens (FWD / REV / PING-PONG), LOOP on/off + vitesse (avant les effets), pitch, gain, pan, filtre, envois reverb / delay, FX TAIL (KEEP / CUT)
- séquenceur 1 à 64 pas, ORDER : GRID / UP / DOWN / UP-DOWN / RANDOM / SHUFFLE / DRUNK, RATE, HOLD, swing, gate, chance, ATTACK / RELEASE
- PLAY WITH NOTE (note longue, calée sur la grille quand l'hôte joue) ou TRANSPORT ; SYNC ou temps libre (FREE TIME, DRIFT)
- REFRESH tous les 1 à 64 temps : positions, loops, reverb, delay re-tirés au hasard (liés ou séparés)
- hasard reproductible : SEED + LOCK ; REVERSE et SCATTER par pas ; SNAP sur transitoires ou passage à zéro
- MIDI : C1 > D#2 = points 1 > 16 ; autres notes = transposition

Voir `INSTALLATION.txt`. Compilation : `cmake -S . -B build && cmake --build build --config Release` (JUCE 8.0.15 téléchargé automatiquement).
Moteur sans JUCE : `Source/PhreakEngine.h` ; tests : `-DPHREAK_BUILD_TESTS=ON`.
