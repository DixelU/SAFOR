# SAFOR – **Simple And Fast** ***Overlaps Remover***   

![example workflow](https://github.com/DixelU/SAFOR/actions/workflows/build.yml/badge.svg)

<p align="center">
<img src="https://user-images.githubusercontent.com/26818917/145869604-80f60168-6c98-44b9-9fe0-4eab5597d748.png" 
    data-canonical-src="https://user-images.githubusercontent.com/26818917/145869604-80f60168-6c98-44b9-9fe0-4eab5597d748.png" 
    width="200" height="200" />
</p>

## Overview

A console application which allows processing MIDIs, removing any note *doubling* or *overlapping* 
while keeping the same visuals as they are presented in the original MIDI.

Sadly, it *does* load a file in memory, but every new event goes through some filtering, 
therefore allowing only *important* notes to be kept in memory.

## Usage

User-friendly experience for MS Windows is provided through a mode selection window and an open file dialogue. 
CLI is also available for all systems for which it was compiled,
but on Windows it is strongly recommended to use the GUI.

Appreciate issue reporting :)
