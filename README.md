just one octave up
===

This [lv2 plugin](https://en.wikipedia.org/wiki/LV2) adds an octaved signal to the original input signal.

By using a different approach than other octaver plugins the quality can be kept high while the latency is kept at the possible minimum

Normally pitch shifting (or octaving) is done either by some kind of fourier transform and manipulation in the frequency space (This has an obvious latency penalty since) or by a rectifying stage (typically done by diodes in a pedal) in a fuzz effect.

This plugin however does the octaving by repeating and inverting each half wave of the input signal and pushing that to the output at 2 times of the input rate.

This approach **cannot** be used to transpose a singal down or up to an arbitrary other interval other than an octave. It is **just one octave up**.

This is a work in progress and it seems I have not solved the latency issue completely. In the meantime one can use [rectifyingOctave.lv2](https://github.com/rominator1983/rectifyingOctave.lv2) which has zero latency and sounds pretty decent after all.

Install
===
Install the pre compiled artifacts from https://github.com/rominator1983/justOneOctaveUp.lv2/releases
- Create a new folder in your lv2 directory (For example ~/.lv2/ or /usr/lib/lv2 or /usr/local/lib/lv2/ would all work in Ubuntu Studio)
- Copy justOneOctaveUp.so, manifest.ttl and justOneOctaveUp.ttl to this new folder.
- Restart/refresh your lv2 host

Build on your own
===
-  `./waf configure`
-  `./waf build`
-  `sudo ./waf install`
- Restart/refresh your lv2 host
