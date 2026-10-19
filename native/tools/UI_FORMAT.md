# Private native UI packet

`export_native_ui.py` writes a new private output directory from original game
content. It reads the existing SGG package/atlas parser and decodes the original
English `AlegreyaSansSCExtraBold.xnb` used by MainMenuScreen's PlayGameButton.
It does not substitute artwork or a host system font.

Run with the existing Pillow-enabled interpreter:

```sh
python3.13 native/tools/export_native_ui.py \
  --content .local/game/base/Content \
  --output .local/native-assets/native-ui-v1
```

`ui.bin` uses little-endian numbers and the following layout:

- Four magic bytes `HUI1`, then uint32 textureCount.
- For each texture: uint32 UTF8-name-byte-count, name bytes, uint32 width,height,
  then exactly width*height*4 bytes RGBA8 in source top-to-bottom row order.
- float32 fontSize, ascent, lineHeight; uint32 glyphCount.
- For each glyph: uint32 Unicode codepoint; seven float32 values atlasX,atlasY,
  width,height,bearingX,bearingY,advance.

Texture names are `font`, `logo`, `background`, `zagreus`, `selector`, and
`selector_backing`. Original transparent canvas size is restored from atlas
trim/original metadata, retaining source positioning. The packet contains ASCII
32 through126; the manifest retains all178 original glyph records, ABC kerning,
source cropping, spacing and default character.

The font's native size is35; its original line spacing is47 and spacing is-1.
Use scale=desiredFontSize/35 for glyph size, bearing, advance and line height;
atlas coordinates stay in original texels. The transport ascent is explicitly35,
not a recovered typographic ascent. Set baseline=lineTop+35*scale. The packet's
bearingY=cropping.Y-35 then reproduces original cropping from line top.
BearingX=cropping.X+ABC.A; advance=ABC.A+ABC.B+ABC.C+spacing. Native behavior for
negative first-glyph A clamping and advanced paragraph layout is not claimed.

The font texture uses white RGB and original Alpha8 coverage, which is straight
RGBA. Sprite decoder channels are preserved verbatim. Physical logo/Zagreus
semitransparent pixels exceed alpha in RGB, excluding a consistently
premultiplied interpretation of those textures. No unproven channel transform
is applied to other source sprites. Use normal source-alpha blending for these
original title and character assets.

`ui-manifest.json` records SHA-256 source identities, atlas entry locations,
original sprite crop/trim, complete original main menu GUI layout and matching
animation definitions. PNGs are previews of the same packet textures. Intro
movies and animated menu layers are not included. This provides assets for a
native menu adaptation; it does not establish original menu script execution.

The bounded native font reader inspection is retained at
`.local/reports/native-ui-font-reader.json` (PDB-matched
`sgg::ReadAssetContent<sgg::SpriteFont>`, RVA0x204b30). It reads the original
texture, rectangles, cropping, character list, three four-byte scalar values,
and vector3 kerning list. The exporter validates complete source consumption,
matching glyph-list counts and every glyph rectangle's atlas bounds.

The exporter also writes independent companions under `ui/`, for the runtime
that consumes the original font layout directly:

- `{font,background,logo,zagreus,selector,selector_backing}.rgba`: four bytes
  `UIR1`, uint32LE width,height, then exact raw RGBA8 bytes.
- `font.bin`: four bytes `UFN1`; uint32LE glyphCount, int32LE lineSpacing,
  float32LE spacing, uint32LE defaultCodepoint. Each glyph is48bytes:
  uint32LE codepoint, int32LE atlasRect[4], int32LE cropping[4], float32LE ABC[3].
  All178 original glyphs are retained. The header is20bytes; source default
  codepoint is42 (`*`). This format preserves original line-top metrics and does
  not require the HUI1 transport baseline policy.

Both packet families contain identical original pixels. The manifest records
SHA-256 identities for every companion file.

The native adaptation stores settings in app-private `native-ui-settings.cfg`.
`HADES_UI_2` is followed by brightness, camera height, touch scale, touch opacity,
left-handed flag and floating-stick flag. The reader still accepts `HADES_UI_1`
without the last field, defaulting to floating controls. Camera height defaults
to1680 and is bounded900..3200; closer views use smaller values. Saving replaces
an adjacent temporary file. These are local UI preferences, not game saves.

The title loads only UI assets. Play enters a loading screen, then the engine
loads scene textures, player assets and original Lua precreation in separate
frames, after presenting the screen. The bar reports these phases, not bytes
or complete original world activation. Failures retain their phase/reason in
Android logs and offer retry/menu navigation. The loader remains synchronous
within each phase, so input cancellation is serviced between phases.

Floating movement begins within the selected lower side of the screen; fixed
movement uses the visible circle. A radial dead zone removes drift. Movement
owns its original pointer until release/cancel; unrelated pointers can pinch
without releasing movement. Pinching while moving requires three contacts.
Resize, control-layout changes, menu navigation and focus loss release movement.
The camera uses elapsed-time smoothing and a pinch-midpoint offset that recenters
after release. This is Android camera/control policy, not original cinematic
camera evidence. `native/analysis/controls_camera_debug.cpp` provides one focused
sanitized replay, and `HadesGestureDebug.java` is a separate emulator shell replay
helper that is never packaged or granted input injection permission in the APK.

The activity handles `smallestScreenSize` as well as screenSize, screenLayout
and orientation, so size changes can resize the native surface without rebuilding
the activity. See Android's [configuration-change documentation](https://developer.android.com/guide/topics/resources/runtime-changes).
The engine cancels active contacts on resize and reconstructs GLES resources on
surface loss; UI preferences are durable, while game progress saves remain
unimplemented.
