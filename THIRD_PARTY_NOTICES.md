# Third-party components and references

Original game content belongs to its respective rights holders and is supplied separately. No game assets are part of this source repository.

## Bundled libraries

Native screenshots additionally use `stb_image_write.h` from the same pinned stb revision `4e9b368bfc071e97206ea5c6adba68dd0cbf8897`, under its included MIT/public-domain license. Upstream header SHA-256: `cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05`. A local patch makes the JPEG bit accumulator unsigned, preserving its intended wrapping shifts without undefined signed overflow. Patched header SHA-256: `6f124ab45c084a89923c06f4bfddd3b723e89334ed31aa5fb63c8460e948c42e`.

- **nlohmann/json 3.12.0**, MIT, by Niels Lohmann and contributors. Header SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`. Full license: [third_party/nlohmann/LICENSE.MIT](third_party/nlohmann/LICENSE.MIT). [Upstream](https://github.com/nlohmann/json/tree/v3.12.0).
- **stb**, revision `4e9b368bfc071e97206ea5c6adba68dd0cbf8897`, MIT or public domain. `stb_image.h` provides original PNG/JPEG decoding; `stb_vorbis.c` supplies Ogg Vorbis decoding (SHA-256 `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`); `stb_truetype.h` is retained for upcoming font rendering. Full license: [third_party/stb/LICENSE](third_party/stb/LICENSE). [Upstream](https://github.com/nothings/stb).
- **miniaudio 0.11.23**, revision `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`, MIT or public domain. Native mixing, decoding and resampling, using SDL for the output device. Header SHA-256 `7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6`. [Upstream](https://github.com/mackron/miniaudio/tree/0.11.23), [license](third_party/miniaudio/LICENSE).
- **SDL2 2.32.10**, revision `5d249570393f7a37e037abf22cd6012a4cc56a71`, zlib license. Fetched by CMake from [libsdl-org/SDL](https://github.com/libsdl-org/SDL/tree/release-2.32.10), whose `LICENSE.txt` is included in that source distribution.
- **wolftools archive modules**, revision `fad1d573a3abb468f2d9a7510b0e77e9be023c45`, MIT. Used by offline data preparation only, never linked into the native application. Includes the DX8 decoder and its constants, binary/text/compression/crypt helpers. [Upstream](https://github.com/popupie/wolftools), [license](tools/vendor/wolftools/LICENSE), [additional notices](tools/vendor/wolftools/THIRD_PARTY_NOTICES.md). The upstream's JSZip, music-metadata and Browser Woditor components are not bundled or used.

## Data-format references

Binary reader structures derive from [djytw/wolf-rpg-formats](https://github.com/djytw/wolf-rpg-formats), revision `5c70e643693e08f99bf299a2ca598f7eaa02c542`, under the MIT license reproduced below. Command framing and enums were also checked against [G1org1owo/wolfrpg-map-parser](https://github.com/G1org1owo/wolfrpg-map-parser), revision `59dc4578c0b7cd925da47e33b02a5173aa4bb856`, MIT, copyright (c) 2025 G1org1o; the same license terms below apply to that reference.

The MIT-declared mruby-wolf package by take-cheeze was inspected when evaluating alternatives and command behavior. Its source is not bundled. Official WOLF documentation was used to distinguish confirmed command behavior from unimplemented behavior. Links and revision details are in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

### MIT license — WOLF format descriptions

Copyright (c) 2024 djytw

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
