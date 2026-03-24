The proposed standard centers around the use of a frequency shift modulation method from which serial clock data can be extracted at rates of up to 300 baud. The system is intended to be used with low to medium cost cassette recorders incorporating electrical stop and start capability which may be operated under program control.

The technique proposed provides for long and short term tape speed variation, limitations in bandwidth due to effects such as tape misalignment, and the necessity to retain low cost and low complexity of the hardware. The technique allows for potential operation at higher tape speed than the nominal 1.875 inch/s (4.75 cm/s).

A mark (logical one) bit consists of eight cycles at a frequency of 2400 Hz.

A space (logical zero) bit consists of four cycles at a frequency of 1200 Hz.

A recorded character consists of a space as a start bit, eight data bits, and two or more marks as stop bits.

The interval between characters consists of an unspecified amount of time at the mark frequency. In this respect the data format is similar to that of asynchronous data communication.

The eight data bits are organized least significant bit first, most significant bit last, and followed (optionally) by a parity bit. The total number of significant bits and the parity bit cannot exceed 8.

Where less than eight data bits are used, the unused bits (following the optional parity bit) at the end of the character are mark bits (2400 Hz).

Data will be organized in blocks of arbitrary and optionally variable length, preceded by a minimum of five seconds of marks. To avoid errors due to splice and wrinkle problems common at the beginning of tape, the beginning of the first data block will occur no sooner than 30 seconds from the beginning of clear leader.
