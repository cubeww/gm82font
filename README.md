# gm82font

`gm82font` is a text rendering DLL for GameMaker 8.2. Its goal is to fill the gap in the original font system, especially for drawing non-English text such as Chinese.

The current implementation uses FreeType, hooks into the GameMaker 8.2 runner's internal texture/render path, caches glyphs per character, and submits quads directly through D3D8.

## Project Layout

```text
gm82font/
  README.md
  gm82font.slnx
  gm82font/
    gm82font.cpp
    gm82font.vcxproj
    third_party/
      freetype/
        include/
        lib/win32/freetype.lib
  project.gm82/
    gm82font.dll
    scripts/
    objects/
    rooms/
```

- `gm82font/gm82font/` is the C++ DLL project.
- `gm82font/project.gm82/` is the bundled GameMaker project copy used for testing.
- FreeType is vendored under `third_party/freetype/`; the project no longer depends on `vcpkg`.

## Build

The main target is `Release|Win32`.

Example build command:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  'gm82font\gm82font\gm82font.vcxproj' `
  /t:Rebuild `
  /p:Configuration=Release `
  /p:Platform=Win32
```

Build output is written directly into `gm82font/project.gm82/`:

- [gm82font.dll](/C:/Users/h7613/Desktop/gm82/gm82font/project.gm82/gm82font.dll)
- [gm82font.pdb](/C:/Users/h7613/Desktop/gm82/gm82font/project.gm82/gm82font.pdb)

## Runtime Model

At the DLL level, only a small core API is exported:

- `gm82font_font_add`
- `gm82font_font_delete`
- `gm82font_last_error`
- `gm82font_string_width`
- `gm82font_string_width_ext`
- `gm82font_string_height`
- `gm82font_string_height_ext`
- `gm82font_set_text_transform`
- `gm82font_set_text_gradient`
- `gm82font_draw_text_ext_transformed_color`

The GameMaker scripts wrap these low-level exports into APIs that feel closer to the original `draw_text*` family.

## Setup In GameMaker

Initialize the DLL first:

```gml
gm82font_init();
```

Create a font and make it current:

```gml
var f;
f = gm82font_font_add("simhei.ttf", 24);
gm82font_font_set_font(f);
```

Delete a font when you no longer need it:

```gml
gm82font_font_delete(f);
```

Free the DLL bindings on shutdown if you want:

```gml
gm82font_shutdown();
```

Read the last error string:

```gml
show_message(gm82font_last_error());
```

## Font API

### `gm82font_font_add(path, size)`

Creates a font handle from a font file.

- `path`: font path or font filename
- `size`: pixel size

Lookup order:

1. The exact path passed from GML
2. The game executable directory
3. `C:\Windows\Fonts`

Return value:

- `>= 0` on success
- `-1` on failure

### `gm82font_font_delete(font)`

Releases the font and all cached glyph textures owned by that font.

### `gm82font_font_set_font(font)`

Sets the current font handle used by measurement and drawing wrappers.

### `gm82font_font_get_font()`

Returns the current font handle tracked by the GML wrapper layer.

## Measurement API

These wrappers use the current font:

- `gm82font_string_width(text)`
- `gm82font_string_width_ext(text, sep, w)`
- `gm82font_string_height(text)`
- `gm82font_string_height_ext(text, sep, w)`

Notes:

- `sep` is line spacing.
- `w` is the wrapping width.
- The `_ext` measurement functions use the same line-breaking logic as the drawing path.

## Drawing API

The GML wrapper layer currently provides:

- `gm82font_draw_text`
- `gm82font_draw_text_ext`
- `gm82font_draw_text_color`
- `gm82font_draw_text_colour`
- `gm82font_draw_text_transformed`
- `gm82font_draw_text_transformed_color`
- `gm82font_draw_text_transformed_colour`
- `gm82font_draw_text_ext_color`
- `gm82font_draw_text_ext_colour`
- `gm82font_draw_text_ext_transformed`
- `gm82font_draw_text_ext_transformed_color`
- `gm82font_draw_text_ext_transformed_colour`

Internally, all of them funnel into the same low-level draw entry point after the wrapper layer sets transform and gradient state.

### Alignment

The drawing code reads the current GameMaker alignment state:

- `draw_set_halign(...)`
- `draw_set_valign(...)`

So alignment works in the same overall style as the native GM text APIs.

### Color

Color variants support 4-corner gradients:

- `c1`: top-left
- `c2`: top-right
- `c3`: bottom-right
- `c4`: bottom-left

## String Rules

To match GameMaker text behavior, the DLL understands GM-style line breaks:

- `#` means newline
- `\#` means a literal `#`

Example:

```gml
gm82font_draw_text(100, 100, "Line 1#Line 2");
gm82font_draw_text(100, 140, "Show a hash \\# here");
```

## Encoding

Incoming strings are decoded like this:

1. Try `UTF-8` first
2. If that fails, fall back to the system `ACP`
3. Convert to internal Unicode/UTF-8 form for glyph lookup and rendering

This makes direct Chinese text usable in the common GM8.2 workflow without needing the earlier byte-list workaround.

## Internal Rendering Flow

The current text pipeline is:

1. GML picks a current font handle with `gm82font_font_set_font`
2. A draw wrapper normalizes parameters into the core draw call
3. The DLL decodes the incoming string and applies GM-style `#` line splitting
4. Layout is computed, including wrap width, line spacing, alignment, scale, and rotation
5. Each codepoint is looked up in a per-font glyph cache
6. On a cache miss, the glyph is rasterized by FreeType and uploaded as a D3D8 texture
7. The glyph quads are tinted and submitted through the runner's internal texture drawing path

The measurement functions share the same layout rules so size queries match the actual render result.

## Demo Project

The bundled demo scene lives at [object1.gml](/C:/Users/h7613/Desktop/gm82/gm82font/project.gm82/objects/object1.gml).

It exercises:

- DLL initialization
- font add / delete / recreate
- current-font selection
- `string_width`
- `string_width_ext`
- `string_height`
- `string_height_ext`
- basic draw
- wrapped draw
- transformed draw
- gradient draw
- wrapped transformed gradient draw

Hotkeys:

- `A`: add font
- `D`: delete font
- `R`: recreate font

## Notes And Constraints

- The current target is the GM8.2 Win32 runner.
- Hardcoded internal runner addresses are used intentionally.
- This assumes the runner code layout matches the version that was reversed.
- If a different runner build changes those internal addresses, the DLL will need adjustment.
- Although the Visual Studio project may still contain x64 configurations, the real rendering path here is 32-bit only.

## Troubleshooting

If text does not render, check the following first:

- `gm82font_init()` returned success
- the font handle from `gm82font_font_add(...)` is valid
- the font file can actually be found
- `gm82font_font_set_font(...)` was called before drawing
- `gm82font_last_error()` contains a useful message

If you pass a font name such as `simhei.ttf`, make sure that file exists either in the game directory or in `C:\Windows\Fonts`.
