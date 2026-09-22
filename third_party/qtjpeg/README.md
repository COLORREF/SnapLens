# Qt JPEG adapter

Unmodified sources from Qt **v6.11.2**, `qtbase/src/plugins/imageformats/jpeg`.
Original URLs and SHA-256 hashes are recorded in `sources.json`; SPDX headers and
upstream license texts are retained. No network access is required at build time.

Only the static SnapLens build uses this adapter. It is compiled against the
Tesseract SDK's **libjpeg-turbo 3.2.0 / JPEG ABI 62** headers and linked against
that same SDK's `jpeg.lib`. The installed Qt 6.11.2 static JPEG adapter expects
ABI 80; `cmake/StaticJpeg.cmake` excludes it from the executable and registers
this compatible static plugin instead. JPG encoding/decoding remains enabled.

The adapter uses private Qt APIs, so its source version must match the static Qt
version. Update all four files, the version guard, source manifest and license
texts together when upgrading Qt. Do not edit JPEG_LIB_VERSION to bypass checks.
