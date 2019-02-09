# SAFOR
**Simple AF Overlaps Remover**

A console application which allows to create midis without any note *doubling* or *overlaping* with keeping the same visuals as in original midi.
Due to the way how algorythm works, i've decided to **sacrafice everything** to keep memory usage as low as possible.
Including: 
* Non-noteon/noteof events and non-tempo events
* Note volumes
* MIDI's length (read bellow)

***First of all, I would notice that this app DISCARDS all the volumes.*** This app on first stages was called **Visual Information Collector**. Not **Audio Information Collector**. As i said, i've done that by this way for higher speed of conversion and lower memory consumption.
**Some information about that:** http://puu.sh/CJBba/5d4ff93b10.png

*The secondary. You should expect some real corruptions on midis with length more than 2^32-1 bytes.*
You can fix it by compiling your own build were you will should replace **DWORD** with **ULI** in the line:
`#define LTE DWORD `.
If you do that, expect a **huge** memory consuption, which is unallowable in most cases.

This app was made completely from scratch *with OOP like structure.*
And sadly it *does* load file in memory, but every new event goes through a tons of code doing filtering and deciding what to do with this.

Appreciate issue reporting :)
