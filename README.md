# Polymorphic Systems 88 audio encoded digital tape decoder

## What is going on here?

A Polymorphic Systems 88 (poly 88 or poly88) is a home computer from the 1970's that
contained an Intel 8080 CPU, up to 64K of RAM, 1K of ROM, and a video display board
that showed 1024 characters of data on a video monitor.

For storage of files to tape, an interface card converts bytes in memory to one of
two different audio formats, which were then recorded on a magnetic cassette recorder.

The primary purpose of the programs in this directory is to
recover files, duplicating the load or recovery process.

When working correctly, the interface card, with software help, is able to
synchronize on the audio signal and recover the binary bits from that signal.

The two formats are called Kansas City Standard (also known as Byte standard),
and Polyphase. The next two sections will briefly describe those standards.

### Kansas City Standard digital encoding

The format of Kansas City Standard data is described in the file KANSAS\_CITY.md

The short version is that a one bit is represented as 8 full waves at 2400Hz,
and a zero bit is represented as 4 full waves at 1200Hz.

The file KansasCity.hpp does a fairly good job of decoding those signals.

Difficulties arise in weak audio signals, because the file KansasCity.hpp
relies on detecting zero-crossings (from negative to positive signal values)
to find the starting points of full-wave cycles. If the signal is weak
enough, peaks may not rise above the 0 value, meaning the wave count will
be lost, and this can cause loss of synchronization to the bits being
loaded.

### Polyphase digital encoding

The polyphase format works by representing a single bit every clock cycle.
By shifting the phase of the signal, you can represent zero and one bits.
The encoding process is called Manchester Encoding.

The file Polyphase.hpp does a fairly good job of detecting polyphase
encoded bits and bytes. It relies on the caller of ByteRead to
identify data leader bytes, 0xe6, which can be used to help
synchronize the audio waveform to the digital bit pattern that was written.

While it does a reasonable job detecting bits, it is very sensitive
to losing synchronization between the audio signal and the digital
bit pattern it is encoding. This problem is describe in a section
below in more detail.

## Record decoding

Digital data is represented on the tape in the following order:

* a series of 8 or more leader bytes to be used for synchronization - these have the value 0xe6
* a SOH (start of header) byte which has the value 0x01
* 8 ASCII bytes of the tape file name
* 1 low order byte of the 2 byte record number
* 1 high order byte of the 2 byte record number
* 1 byte represening the length of the data following the header
* 1 low order byte of the 2 byte address or offset of the data
* 1 high order byte of the 2 byte address or offset of the data
* 1 byte record type
  * 0x00 == binary
    * program code, to be store at the given address
  * 0x01 == comment
    * contains a string that is written to the console on load
  * 0x02 == end of file
    * marks the end of a complete file
  * 0x03 == autoexecute
    * also an end of file, but when read, we jump to the address
      to begin program execution.
  * 0x04 == data
    * the address is actually an offset, not a physical
      memory address.
* 1 byte header checksum

The arithmetic sum of the above bytes is intended to be 0.

The above header data is immediately followed by a set of data bytes:

* referring the the length from the header, there will be that number of data bytes here (0 means 256 bytes of data)
* 1 byte data checksum

The sum of the above data bytes with the data checksum is also intended to be 0.

## File format

One tape file is composed of 1 or more records (described above).

Records are listed in data order, one after the other, with leader bytes (0xe6)
ahead of every record.

## The problem

As magentic tapes age, and as tape players change and age, it becomes more
difficult to get a clean WAV file represenation as it was originally saved
on the magnetic tape.

Although we can get a fairly high sample rate, so we have good fidelity of the
remaining signal, several kinds of noise appear to alter the signal.

One is AC (alternating current) related noise, which shows up as a 60Hz
or 120Hz wave overlaying the 4800Hz signal.

Another is actual tape dropouts due to magnetic particles becoming
physically damaged. These can show up as a signal of zero where
before it might have tracked the waveform smoothly.

As a consequence, decoding the manchester encoded signal becomes more
error-prone, with only modest ability to recover due to information
repitition.

Of the two encodings mentioned above, the Kansas City Standard has
the most duplication of information, because each bit is represented
by up to 8 full waves at 2400Hz.

The polyphase, or manchester encoded signals, are encoded as a single
bit per 4800Hz cycle. This means small amounts of phase shift, noise,
or physical damage can easily damage one or more bits that we want
to recover.

## The recovery strategy

In both tape formats, there will be errors in the underlying magnetic
to bit translation.

The end goal is to load a complete set of Record objects that describe
a complete file. Ideally, there would be no errors.

In reality, some records may load just fine, but others will have
a problem with the tape WAV file and will lose syncrhonization, or
get start/stop bit errors, or header or data checksum errors, for
example.

So I believe the goal is going to be allowing the user to scan the
entire WAV file for all records it can find, record those, then
using those records to locate other missing records, or partial
reads.

Once we have the approximate locations of missing records, or
the likely exact location of damaged records, we will want to
be able to see each portion of that record in relation to the
underlying WAV file waveform.

Next, the user should be able to drag and move portions of the
waveform, ideally respecting some kind of temporally local
rules to ensure smooth transitions to and from the edited
spots. Maybe click and drag a wave form point up or down, maybe 
move curve left or right slightly to change the phase of
the signal at a given location.

We may need two visual strategies, one to deal with byte phase 
(Kansas City Standard), and another for polyphase.

In any case, I want to be able to see records that can be
scanned and found automatically. I want to be able to
edit WAV waveform points. I want to be able to right
click at a waveform location and re-scan. I want to always
see the values in the Record, including leader bytes,
and checksum bytes, in relation to the WAV file samples.
I want to be able to see very fine tic marks indicating
start and stop of bytes, and optionally bits in a WAV
file.

All of this will allow me to explore how to recover the
missing bits in the polyphase and byte format files.
