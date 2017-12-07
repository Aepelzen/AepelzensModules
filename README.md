# VCV Rack plugins

![screenshot](https://github.com/Aepelzen/AepelzensPlugins/blob/master/screenshots/GateSEQ8.png)

## GateSeq

A Gate Sequencer intended for polyrhythms. Every channel has it's own clock input and length. There is also a global clock input and an internal clock.
Furthermore each channel has a probability settings that sets the probability that an active beat will be sent out.

## QuadSeq

A four channel sequencer. Like GateSeq each channel has it's own clock input (the 4 inputs on the bottom left) and length.
The mode parameter sets one of the following playback modes:
* Forward
* Backwad
* Alternating
* Random Neighbour
* Random

## Burst

A Burst generator. For every received trigger a number of triggers is sent out.
Repetitions and time set the number of triggers and the time between them. Accelerations shortens the time between subsequent triggers, jitter shifts them randomly.
The last trigger also triggers the EOC (end of cycle) output. You can connect this to the trigger input and turn up jitter and/or acceleration to get an irregular clock or chain multiple burst generators together.
