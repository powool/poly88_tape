Introduce a visible "Waveform Selection" concept.

There should be a single pair of "selected waveform start" and a "selected
waveform end" value for the UI. The user will control-left click
to set the selected waveform start, and typically, the UI will automatically
determine the selected waveform end using various context sensitive calls.

Put a new identifying (static) label in front of the "Index/Width/Tape File/Record Number"
set of record related labels and assign the new label the name "Selected Record"

Above that row, put a new row of static and dynamic labels:

The new row should start with the identification label "Selected Waveform".
Follow that with "Index:" (the index of the start of the selected waveform),
"Width:" (the width of the selected waveform), followed by two values, shown
as both binary format (e.g. 0b1011001) and hex (e.g. 0xe6).

The first value comes from calling DataInterface::ReadByteWithBits with the
user chosen waveform index. Then call DataInterface::ReadByteWithBits to
obtain the second value, but this time set the selected waveform end value
with the end of the final bit returned from DataInterface::ReadByteWithBits.

When the selection exists, highlight it in a faint transparent green the
waveform range in which that lies. In addition, at every known bit index, omit the highlight.
This will allow me to investigate where bytes and bits start and stop on the waveform.
I will have to zoom in quite far to see the bit boundaries. This is fine.

The selection highlighting should remain until a new selection is chosen or
a new file is loaded.

The selection data display should be updated any time the waveform is
editted by the user.
