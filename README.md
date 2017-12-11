# VCV Rack plugins

![screenshot](https://github.com/Aepelzen/AepelzensModules/blob/master/images/screenshot.png)

## GateSeq

A Gate Sequencer intended for polyrhythms. Every channel has it's own clock input and length. There is also a global clock input and an internal clock.
Furthermore each channel has a probability setting that sets the probability that an active beat will be sent out.

## QuadSeq

A four channel sequencer (The knobs are made by bogaudio). Like GateSeq each channel has it's own clock input (the 4 inputs on the bottom left) and length. The mode parameter sets one of the following playback modes:
* Forward
* Backwad
* Alternating
* Random Neighbour
* Random

## Burst

A Burst generator. For every received trigger a number of triggers and an accompanying CV signal is sent out.
Repetitions and time set the number of triggers and the time between them. Acceleration shortens the time between subsequent triggers, jitter shifts them randomly. The switch at the bootom controls the output mode (trigger or gate).

The last trigger also triggers the EOC (end of cycle) output. You can connect this to the trigger input and turn up jitter and/or acceleration to get an irregular clock or chain multiple burst generators together.
The mode button selects the mode for the CV output, that can be used to modulate pitch or filter cutoff for delay style effects etc. Currently the following modes are supported:
* Up: cv increases in even steps
* Down: cv decreases in even steps
* Alternating1: output alternates between positive and negative values. The magnitude is only increased after 2 pulses (so you get 0, 1, -1, 2, -2 ...)
* Alternating2: output alternates between positive and negative values. The magnitude is increased after every pulse (so you get 0, 1, -2, 3, -4 ...)
* Randomp: positive random values
* Randomn: negative  random values
* Random

If you have ideas for additional CV-Modes or any other suggestions please let me know.

## New Module: Manifold

A wavefolder. Works best with simple input signals like sine or triangle waves. The fold and symmetry inputs work well with CV and audio signals. The output becomes pretty noisy for high frequency modulators but produces very interesting sounds at low/mid frequency ranges. There is an alternative folding algorithm that can be switched via context menu. That one does all the folding in a single pass and therefore the stages button does nothing if this mode is selected. It also responds different to the symmetry parameter, especially with a high number of folds.

Note: this module shifts the phase of the input-signal (because of the upsampling)

# Building

The wavefolder uses libsamplerate for the upsampling. That shouldn't be a problem because Rack depends on libsamplerate anyway but you might have to install the header-files if you don't build Rack yourself.
