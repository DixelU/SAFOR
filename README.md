# SAFOR
**Simple AF Overlaps Remover**

A console application which allows to create midis without any note *doubling* or *overlaping* with keeping the same visuals as in original midi.
Due to the way how algorythm works, i've decided to **sacrafice everything** to keep memory usage as low as possible.
Including: 
* Non-noteon/noteof events and non-tempo events
* MIDI's length (read bellow)

*You should expect some real corruptions on midis with length more than 2^32-1 ticks.*
You can fix it by compiling your own build were you will should replace **DWORD** with **ULI** in the line:
`#define LTE DWORD `.
If you do that, expect a **huge** memory consuption, which is unallowable in most cases.

This app was made completely from scratch *with OOP like structure.*
And sadly it *does* load file in memory, but every new event goes through a tons of code doing filtering and deciding what to do with this.

Appreciate issue reporting :)

> How do i compile this thing?

Get Dev-CPP and plug it in.  
Check out for build-in complier to be percicely `TDM-GCC 4.9.2`.  
Complier flags: `-std=c++14`  
Linker flags: `-static -lcomdlg32`
