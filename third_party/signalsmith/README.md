Signalsmith Stretch: makes a recording longer or shorter without changing
its pitch. Copied unchanged; both parts are MIT licensed (see the LICENSE
files here).

- `signalsmith-stretch.h` from https://github.com/Signalsmith-Audio/signalsmith-stretch
  at commit 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da
- `signalsmith-linear/stft.h` and `fft.h` from https://github.com/Signalsmith-Audio/linear
  at commit 7bb1c8f36794d6980080ce817c91f8da8fe56a66

Only `app/stretch_smooth.cpp` includes it. It runs ahead of time on a
worker thread, never on the audio thread.
