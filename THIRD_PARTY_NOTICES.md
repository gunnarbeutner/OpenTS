# Third-party notices

OpenTS binaries include software from the following projects. Each project
remains under its own license and copyright notices.

| Project                                                            | Use                                       | License      |
| ------------------------------------------------------------------ | ----------------------------------------- | ------------ |
| [bgfx](https://github.com/bkaradzic/bgfx)                          | Rendering                                 | BSD 2-Clause |
| [bx](https://github.com/bkaradzic/bx)                              | Foundation library used by bgfx           | BSD 2-Clause |
| [bimg](https://github.com/bkaradzic/bimg)                          | Image and texture processing used by bgfx | BSD 2-Clause |
| [DirectX-Headers](https://github.com/microsoft/DirectX-Headers)    | Direct3D API headers used by bgfx         | MIT          |
| [tinystl](https://github.com/mendsley/tinystl)                     | Containers used internally by bgfx        | BSD 2-Clause |
| [astc-encoder](https://github.com/ARM-software/astc-encoder)       | ASTC texture processing used by bimg      | Apache-2.0   |
| [OpenGL Registry](https://github.com/KhronosGroup/OpenGL-Registry) | OpenGL API headers used by bgfx           | MIT          |
| [Vulkan Headers](https://github.com/KhronosGroup/Vulkan-Headers)   | Vulkan API headers used by bgfx           | Apache-2.0   |
| [miniaudio](https://github.com/mackron/miniaudio)                  | Audio device output, resampling, and WAV, FLAC, and MP3 decoding | MIT-0 or Unlicense |
| [stb_vorbis](https://github.com/nothings/stb)                      | Ogg Vorbis decoding, bundled with miniaudio | MIT or Unlicense |
| [LZO](https://www.oberhumer.com/opensource/lzo/)                   | LZO1X compression for maps, saves, and network blocks | GPL-2.0-or-later |
| [RmlUi](https://github.com/mikke89/RmlUi)                          | User interface documents, styling, and layout | MIT |
| [robin_hood](https://github.com/martinus/robin-hood-hashing)       | Hash containers bundled with RmlUi        | MIT |
| [itlib](https://github.com/iboB/itlib)                             | Containers bundled with RmlUi             | MIT |
| [FreeType](https://freetype.org/)                                  | Glyph rasterization for RmlUi             | FTL or GPL-2.0-or-later |
| [Liberation Fonts](https://github.com/liberationfonts/liberation-fonts) | The user interface text face          | OFL-1.1 |
| [ICU charset data](https://github.com/unicode-org/icu-data)        | The code page 437 table in `code/codepage.cpp` | Unicode-3.0 |
| [Windows best fit tables](https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WindowsBestFit/) | The Windows-1252 table in `code/codepage.cpp` | Unicode data terms |

The source checkout keeps the license texts under `thirdparty/`. Binary
packages reproduce the license texts for the components used by OpenTS under
`OpenTS_THIRD_PARTY_LICENSES/`.
