# Third-Party Licenses

## Schwung plugin API header

`src/host/plugin_api_v1.h` is copied unchanged from
[charlesvestal/schwung](https://github.com/charlesvestal/schwung) (`src/host/plugin_api_v1.h`,
commit e877ed64b2b727f5f928c7faa87fef1e12171286), under the MIT License:

```
MIT License

Copyright (c) 2025-2026 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## uPD933 sound chip emulation (MAME)

`src/dsp/upd933.c` and `src/dsp/upd933.h` are a C port of MAME's
`src/devices/sound/upd933.cpp` and `upd933.h`
([mamedev/mame](https://github.com/mamedev/mame), commit
34efd1218ad50ae1a532a29e996ea4258042e145), copyright Devin Acker, under the
BSD 3-Clause License:

```
BSD 3-Clause License

Copyright (c) Devin Acker

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## and-kal test patches

`tests/fixtures/banks/and-kal/*.syx` (used by the tests, not shipped) are
CZ-101 tones from
[and-kal/Casio_CZ101-SysExes](https://github.com/and-kal/Casio_CZ101-SysExes),
released into the public domain under the Unlicense (copy in that folder).

## Sources of engine data

The CZ firmware behaviour in `src/dsp/cz_synth.c` and `src/dsp/cz_tone.c` is
written fresh from the research in `reports/Casio CZ synthesis engine.md`. No
code was copied from the sources it cites. The numeric tables (key follow
curve and multipliers, DCO cap, vibrato expansion) are values read from the
CZ-101 v2 firmware through ajxs's annotated disassembly, cross-checked against
Louis Yoshinaga's hardware measurements
([LouisYOSHINAGA/pd](https://github.com/LouisYOSHINAGA/pd), MIT). The DCA
0.5 dB-per-code law and the 1/8-semitone DCO unit follow those measurements.
