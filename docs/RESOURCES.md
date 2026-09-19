# Resources, initialized lists, and library packages

sCr++ has one execution sprite, `Program`. Resource packages provide names and
assets, not additional sprites. Every linked costume belongs to `Program`, so
switching costumes, showing/hiding, movement, and stamping operate on the same
sprite. The required Scratch stage is not an additional execution sprite.

## Manifest

Place `sCrpp.toml` alongside the template build script:

```toml
version = 1

[package]
name = "game"

[[costumes]]
file = "idle.png"

[[costumes]]
file = "art/character.run.svg"
name = "run"
size = [48, 64]
center = [24, 60]

[link]
resources = ["../shared/sCrpp.toml"]
```

- `package.name` is the resource namespace. It need not equal the directory name.
- Each `[[costumes]]` entry explicitly names a file. An empty list is valid; the
  builder does not scan a directory implicitly.
- A bare filename such as `idle.png` resolves to `resources/idle.png` beside the
  manifest. A path containing a directory, such as `art/idle.png` or `./idle.png`,
  resolves relative to the manifest itself. Backslashes in paths are accepted and
  normalized; forward slashes are recommended for portable manifests.
- `name` defaults to the basename with its last extension removed:
  `character.run.svg` becomes `character.run`.
- The final Scratch costume name is `package::name`, for example `game::idle`.
  Both names may contain Unicode, but must be nonempty and contain neither `::`
  nor control characters. Duplicate full names are errors.
- `size = [width, height]` sets positive logical display dimensions. It does not
  discard or resample the PNG's source pixels.
- `center = [x, y]` sets the rotation center in final logical costume coordinates;
  the default is half the final width and height. Centers outside the canvas are
  allowed. For an SVG with a nonzero viewBox origin, these are viewport coordinates,
  not the original viewBox coordinates.
- `[link].resources` lists additional independent manifests relative to this
  manifest. Dependencies may link other resource packages. Repeated references to
  the same manifest are deduplicated; cycles and two different manifests declaring
  the same package name are errors.

The primary manifest supplies the package name for the template's generated C++
wrapper. Linked package assets remain accessible by their complete names. A
resource package does not require an accompanying LLVM module.

## Initialized native lists

Resource packages can initialize Scratch-native lists from UTF-8 text files:

```toml
[[lists]]
name = "font"
file = "font.txt"
readonly = true

[[lists]]
file = "tables/messages.txt"
```

The same file lookup rules apply as for costumes: `font.txt` means
`resources/font.txt`, while `tables/messages.txt` is relative to the manifest.
The file extension must be `.txt` (case-insensitive). `name` defaults to the
filename without its last extension. The resulting list names are `game::font`
and `game::messages` for package `game`. Lists and costumes have separate name
spaces. Two list declarations with the same full name are errors.

Each LF-delimited line becomes one **string** item; numeric-looking lines remain
strings. A UTF-8 BOM is accepted and removed. One CR immediately before LF is
removed, so both LF and CRLF files work. All other data is preserved, including
leading/trailing spaces, blank rows, embedded standalone CR, U+2028, and U+2029.
A terminal LF terminates the last row rather than adding another one:
`a\n` has one item, `a\n\n` has two, `\n` has one empty item, and an empty file
has no items. Invalid UTF-8 and U+0008 are rejected; Scratch removes U+0008 during
project loading. The limit is 200,000 items per list.

`readonly` defaults to `false`. If true, the compiler rejects generated list
mutation blocks targeting that list. This is a compiler check, not a protection
against manually editing the Scratch project. Readonly lists are useful for
font data and numeric lookup tables.

List items are embedded directly in the prepared `resources.json` and then in
`project.json`; the original TXT is not needed after preparation. These lists
are outside the LLVM byte-memory list, so large font data does not consume guest
heap or stack space. Assembly refers to the qualified native list name. An empty
list previously declared by assembly can receive the resource initialization;
an existing nonempty initial list is a link conflict. As with ordinary Scratch
lists, runtime mutations persist until code explicitly resets them; a green flag
does not automatically reload the TXT.

## Native event collectors

Event declarations collect wheel-like arrow-hat triggers or keyboard presses:

```toml
[[events]]
type = "wheel"
queue = "wheel"
enabled = "__scl_events_enabled"
capacity = 128

[[events]]
type = "keyboard"
queue = "keyboard"
enabled = "__scl_events_enabled"
capacity = 128
```

`queue` is package-local and becomes `game::wheel`; `enabled` is the exact global
Scratch variable name and is **not** package-qualified automatically. The queue
is created empty if absent and must be mutable. The enable variable is created
with initial value `0` if absent. Library initialization enables collection.
`capacity` defaults to 128 and must be an integer from 1 to 200,000. Only
`type = "wheel"` and `type = "keyboard"` are supported; declarations do not
register arbitrary LLVM/C++ callbacks.

The generated up/down key hats call short warp collectors that check the enable
flag and the corresponding `key pressed?` state. Only when that key is **not**
currently held do they enqueue a wheel direction. Collectors do not wait, draw,
or enter the LLVM runtime. Library code consumes the queue in the main execution
context; this avoids multiple Scratch threads sharing an LLVM stack.

Keyboard packs generate 86 key hats sharing one collector: 68 printable non-space
ASCII keys (one hat per case-insensitive letter), space, enter, four arrows, and 12
TurboWarp extra keys. Entries are numeric key codes documented in
`include/events/events.hpp`. Original Scratch does not fire the extra-key hats.
TurboWarp letter events are converted to lowercase unless Shift is held when
the collector executes; Caps Lock is not modeled. Original Scratch keeps
uppercase letters. Only the up/down keyboard hats require a live pressed state, to exclude wheel
triggers; other keys remain queued even when released before their hat executes.
This does not expose key-up notifications, IME text, or unsupported OS keys such
as Tab, Alt and function keys. Packs using the same enable variable also share
its dropped-event counter.

This is a compatibility technique, not a native wheel device API. Holding an
arrow while scrolling in that direction can suppress scrolling. A quick arrow
press and release before its hat executes can be mistaken for scrolling, and
rapid hat events may coalesce. Exact wheel delta and perfect separation from
keyboard input are unavailable through these vanilla Scratch blocks. The
console intentionally uses this wheel-only inference for its first scrolling
implementation, rather than treating held arrow keys as scrolling.

## Linking library source with resources

The template build script also accepts library source declarations:

```toml
[library]
sources = ["console.cpp"]
include_dirs = [".."]

[link]
resources = ["../pte/sCrpp.toml", "../events/sCrpp.toml"]
```

All `[library]` paths are relative to their own manifest, including bare
filenames; the `resources/` fallback applies only to costume/list data files.
`sources` accepts existing `.cpp`, `.cc`, or `.cxx` files, and `include_dirs`
accepts existing directories. Both fields are optional arrays; unknown library
fields are errors. The template walks the linked manifest graph, deduplicates
resolved source/include paths, compiles the library sources with the project,
and links their LLVM modules along with every prepared resource pack. Headers
can therefore contain declarations while implementation and font data live in
the same independent library package. This does not create another sprite.

The standalone `build_resources.py` command prepares resources only. It does not
compile `[library].sources`; custom build systems must compile and link those
sources themselves (or use separately prepared bitcode). Prepared JSON contains
data and event descriptions, not executable source paths or automatic callbacks.

## PNG and SVG behavior

PNG files are wrapped in SVG containing a Base64-encoded copy of the **original
PNG bytes**. For example, a 1920 x 1080 PNG with `size = [480, 270]` displays in a
480 x 270 logical viewport while retaining all source pixels in the SB3. Without
`size`, one source pixel corresponds to one logical unit. Images are never
automatically fitted to the 480 x 360 stage. Explicit PNG width and height may
stretch the aspect ratio.

SVG files retain their vector content inside an outer SVG canvas whose width,
height, and `viewBox="0 0 width height"` all match the logical display dimensions.
The original SVG becomes an inner viewport, retaining its viewBox (including its
origin), aspect-ratio rule, styles, and transforms. A missing original viewBox is
derived from the original dimensions. This extra canvas matters because Scratch
normalizes an outer viewBox when determining costume dimensions. Absolute source
dimensions support unitless values,
`px`, `in`, `cm`, `mm`, `pt`, and `pc` at 96 DPI. A valid viewBox can supply missing
dimensions. Percentage/relative dimensions are rejected; replace them with
absolute dimensions or omit them and provide a viewBox. An explicit `size`
changes the viewport; the source SVG's `preserveAspectRatio` behavior remains in
effect.

Resources must be self-contained: SVG local `#id` references and embedded raster
images are accepted. External images/stylesheets, scripts, event attributes,
`foreignObject`, document types/entities, and CSS resource imports are rejected.
These checks define the accepted resource format; they are not a general-purpose
SVG sanitizer. PNG headers/chunk checksums and SVG XML/geometry are validated
before writing a prepared pack. The browser remains responsible for rendering
valid image data.

All prepared costumes have `dataFormat = "svg"` and `bitmapResolution = 1`.
Rotation centers therefore always use logical SVG units, avoiding bitmap
resolution conversions. Actual stamp resolution still depends on the host's pen
layer; retaining source pixels does not enable TurboWarp's high-quality pen mode
in original Scratch.

## Portable preparation and linking

The resource feature requires **Python 3.11 or newer** for the standard-library
TOML parser and needs no third-party Python packages.

Prepare one independent pack:

```sh
python tools/build_resources.py --manifest path/to/sCrpp.toml --output-dir build/resources/game
```

The output is portable and contains no references to the original source paths:

```text
build/resources/game/
  resources.json
  assets/
    <md5>.svg
```

`resources.json` contains `schemaVersion: 1`, a `package` name, `costumes`,
`lists`, and `events`. Lists contain qualified `name`, string-array `items`, and
boolean `readonly`; events contain validated input collector configurations.
The compiler also accepts older prepared packs without `lists` or `events`.
Each costume contains standard SB3 costume metadata plus a relative `path` to its
SVG. Identical prepared image bytes share one file, even when names or rotation
centers differ. XML normalization can change SVG file bytes; the digest identifies
the prepared asset rather than the source file.

Pass prepared packs to the compiler:

```sh
scratch-llvm main.bc --resources build/resources/game/resources.json -o project.sb3
```

Repeat `--resources` to link multiple packs. Files are stored under their standard
`<md5>.svg` names in the SB3 root and registered in `Program.costumes`; the prepared
pack's `assets/` directory is an intermediate layout only. No resource tree
shaking is performed: all explicitly declared costumes are packaged.

The standalone preparation command prepares exactly one manifest. A build system
can call `collect_manifests(root)` to resolve the full graph, then
`prepare_resources(manifest, output_dir)` for each manifest and pass the returned
JSON paths to the compiler. The template performs this automatically.

## Runtime names and strings

The runtime interface `void set_string(const std::string&)` writes text into the
Scratch-native variable `__scl_string`. It is shared temporary state on the single
execution sprite. A costume wrapper can construct `package + "::" + name`, call
`set_string`, then use `__scl_string` as the `looks_switchcostumeto` input. Text is
not reinterpreted as executable assembly.

The public C++ name is `scratch::set_string(const std::string&)`; it is declared
in the template's `scratch.hpp` and SDK `scratch_string.hpp`. Valid UTF-8,
supplementary characters (including emoji), case and embedded NUL are preserved.
Malformed bytes each produce U+FFFD. U+0008 (backspace) also produces U+FFFD,
because vanilla Scratch's project parser removes this character from loaded
strings. Resource names reject control characters, so this does not affect valid
costume identifiers. A read-only three-item Unicode table is included only if the
program actually uses the conversion; it is outside the guest byte-memory list.

The template provides `scratch::set_costume(name)` for its primary package and
`scratch::set_costume_qualified("other::name")` for any linked package. Both call
the official costume block directly; unknown names are not validated at runtime.
The other new wrappers are `scratch::show()`, `scratch::hide()` and
`scratch::stamp()`. A resource-enabled project starts each green flag with the
blank costume selected and Program hidden.

An unknown fully qualified name follows Scratch's normal behavior: the current
costume stays selected. A subsequent stamp therefore stamps the previous costume.
Showing/hiding the sprite does not erase existing stamps; the pen erase operation
clears them.

The build-generated `scratch.cpp` belongs in the build directory, while the public
header supplies declarations. This keeps the package name in the manifest rather
than duplicating it in user-written C++ headers.

## Verification

```sh
python tests/test_resources.py
```

The tests cover original PNG-byte preservation, display dimensions, centers,
Unicode names, path resolution, file deduplication, deterministic output, SVG
units/viewBox handling, malformed resources, manifest validation, and linked
package graph errors, exact TXT line preservation, list capacity, and event
configuration. The native `resources-test` additionally verifies real resource
linking and SB3 list serialization, readonly metadata, conflicts, and rejection
of malformed prepared data. Rendering, runtime switching, and stamping require the
separate VM/browser integration checks.
