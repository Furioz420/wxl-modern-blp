# wxl-modern-blp

WarcraftXL extension for modern BLP texture compatibility and the 32-bit client's texture memory budget. It normalizes supported modern BLP encodings, caps existing mip chains by texture class, converts character equipment components to the paletted format expected by the legacy skin compositor, and handles the two rain textures that otherwise render dark under Wrath's blend mode.

## Build and configuration

Place this repository at `wxl-core/extensions/wxl-modern-blp`, configure Win32, and build the `wxl-modern-blp` target. The source built in Release/Win32 against Furioz420/wxl-core `853217d7b0441e95eed2ba092f291dbd6626e6c9` on 2026-10-06. This is a source compatibility check, not a packaged client release or gameplay acceptance.

The supplied `wxl-modern-blp.cfg` documents and sets separate maximum edges for interface art, loading screens, world textures, and equipment components. Its rain switch applies only to `RainDrop01` and `RainDropSplash01`. Keep this configuration with the DLL and check client output in representative outdoor, city, loading-screen, and character-equipment scenes before release.

## Credits and license

WarcraftXL source notices and the GPL-3.0 license are retained. The Furioz420 integration changes are attributed by Git history.
