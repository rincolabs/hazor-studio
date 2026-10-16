# DCP camera profiles — attribution and license

Hazor distributes 161 Adobe DNG Camera Profile (`.dcp`) files plus
`camera_model_aliases.json`, taken as-is from RawTherapee's
`rtdata/dcpprofiles/` (see [`src/rawengine/PROVENANCE.md`](../src/rawengine/PROVENANCE.md)
for the vendoring details). They are distributed under
`3rdparty/rawengine/rtdata/dcpprofiles/` — not tracked in git, same as the
ONNX models and RealESRGAN binaries elsewhere in this repo.

## Not Adobe-derived

The string `com.adobe` present in these files is only the XML namespace of
the DNG/DCP tag format itself — it does not indicate Adobe authorship or
that the profiles are derived from Adobe-supplied data.

## Authorship (per embedded copyright strings)

Counted directly from the 161 `.dcp` files:

| Copyright holder | Count |
|---|---|
| Maciej Dworak | 63 |
| Generated with [DCamProf](https://www.ludd.ltu.se/~torger/dcamprof.html) v0.10.4 | 2 |
| Dr Slony | 1 |
| (no embedded copyright string) | 95 |

## License

Distributed by RawTherapee under **GPL-3.0-or-later**, same as the
RawTherapee project itself and compatible with Hazor's own GPL-3.0 license.

## Origin

`rtdata/dcpprofiles/` in the RawTherapee source tree, vendored as described
in [`src/rawengine/PROVENANCE.md`](../src/rawengine/PROVENANCE.md).
