# Polymorphic Systems 88 audio encoded digital tape decoder

## What is going on here?

A Polymorphic Systems 88 (poly 88 or poly88) is a home computer from the 1970's that
contained an Intel 8080 CPU, up to 64K of RAM, 1K of ROM, and a video display board
that showed 1024 characters of data on a video monitor.

For storage of files to tape, an interface card converts bytes to one of two different
audio formats, which were then recorded on a magnetic cassette recorder.

The primary purpose of the programs in this directory is to
recover files, duplicating the reverse process. The interface card, with software
help, is able to synchronize on the audio signal and recover the binary bits from
that signal.

The two formats are called Kansas City Standard, and Polyphase. The next two
sections will briefly describe those standards.

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
The process I believe is called Manchester Encoding.

The file Polyphase.hpp does a fairly good job of detecting bits and
bytes. It relies on the caller of ByteRead to identify data leader
bytes, 0xe6, which can be used to help synchronize the audio to the
digital bit pattern that was written.

While it does a reasonable job detecting bits, it is very sensitive
to losing synchronization between the audio signal and the digital
bit pattern it is encoding.

## Record decoding

Digital data is represented on the tape in the following form:

* a series of 8 or more leader bytes to be used for synchronization - thse have the value 0xe6
* a SOH (start of header) byte which has the value 0x01
* 8 ASCII bytes of the tape file name
* 1 low order byte of the 2 byte record number
* 1 high order byte of the 2 byte record number
* 1 byte represening the length of the data following the header
* 1 low order byte of the 2 byte address or offset of the data
* 1 high order byte of the 2 byte address or offset of the data
* 1 byte record type
  * 0x00 == binary  0x01 == comment  0x02 == end of file  0x03 == autoexecute 0x04 == data
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
