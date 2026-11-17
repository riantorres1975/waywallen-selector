# Third-party notices

## Waywallen protocol

`protocol.proto` is a modified, reduced subset of the control protocol from
[Waywallen v0.5.0](https://github.com/waywallen/waywallen/blob/v0.5.0/proto/control.proto),
checked against [v0.6.0](https://github.com/waywallen/waywallen/blob/v0.6.0/proto/control.proto),
licensed under GNU GPL version 3. Original work belongs to the Waywallen
contributors; this project is an independent companion, not an official release.

Modifications dated 2026-11-04:

- Only the requests and responses used by this selector are declared.
- Global settings retain unmodelled fields through `UnknownFieldSet`.
- Plugin-map entries are preserved as opaque wire-compatible payloads.
- The protobuf package is renamed to `selectorwire`.

Compatibility update dated 2026-11-16: display role (field 25) is retained to
exclude lock-screen surfaces in both supported versions.

The repository is distributed under GPL-3.0-only. See [LICENSE](LICENSE).

## Runtime/build dependencies

Qt 6, KDE Frameworks 6, Protocol Buffers and SQLite are external dependencies.
Their source or binaries are not bundled in this repository. Their respective
licenses continue to apply to those components.

## Wallpapers

No original wallpaper files, Workshop projects or personal settings are included.
The documentation screenshots contain reduced previews from an example library
solely to demonstrate the selector. Those images belong to their respective
authors; the project's GPL license does not relicense the depicted artwork.
Images shown locally come from each user's existing Waywallen library and remain
subject to their original authors' licenses.
