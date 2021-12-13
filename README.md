# SAFOR - **Simple And Fast** ***Overlaps Remover***   

![example workflow](https://github.com/DixelU/SAFOR/actions/workflows/main.yml/badge.svg)

<p align="center">
<img src="https://user-images.githubusercontent.com/26818917/145869604-80f60168-6c98-44b9-9fe0-4eab5597d748.png" data-canonical-src="https://user-images.githubusercontent.com/26818917/145869604-80f60168-6c98-44b9-9fe0-4eab5597d748.png" width="200" height="200" />
</p>

A console application which allows to process MIDIs, removing any note *doubling* or *overlaping* while keeping the same visuals as they are presented in the original MIDI.
Sadly it *does* load file in memory, but every new event goes through some filtering, therfore allowing to keep only *important* notes.

Appreciate issue reporting :)
