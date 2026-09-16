# Fire Extinguisher source record

- Asset: `Fire Extinguisher`
- Author: JamesWhite
- Publisher: OpenGameArt.org
- Published: 2015-01-24
- Canonical page: https://opengameart.org/content/fire-extinguisher
- License: CC0 1.0 Universal
- License link: https://creativecommons.org/publicdomain/zero/1.0/
- Retrieved: 2026-09-06 (Asia/Seoul)
- Original archive URL: https://opengameart.org/sites/default/files/fire_extinguisher_model_0.zip

## Integrity

| File | Bytes | SHA-256 |
|---|---:|---|
| `fire_extinguisher_model_0.zip` | 155,104 | `21F0C4E33C4D245DA57F54BE9BC5742C9EC2C24963CA1D296BE99D37EE0C2B38` |
| `fire_extinguisher_model.obj` | 28,579 | `61A606C4A068A1CA8F351F908F2A616DF5972D6EC46AED7289A695A0CA72F5DA` |
| `fire_extinguisher_diffuse.tif` | 130,666 | `DDFC94D8F9566237AC902BAA4BD1E69D7E9657B675A873006F7E208F411B33AB` |

The original archive contains the OBJ, diffuse texture, and preview image. Its
OBJ references `fire_extinguisher_model.mtl`, but that MTL is absent from the
archive. This project supplies a minimal MTL that binds the declared
`fire_ex_SG` material to the included diffuse texture; it does not modify the
downloaded geometry or texture.

The OBJ has 187 positions and 165 polygon faces. Its authored coordinate range
is approximately 1.78 x 6.22 x 6.47 units, so the Unreal actor applies a 10x
visual scale to obtain an extinguisher approximately 62 cm tall.

## Project use

The source page marks the work as CC0. It may therefore be copied, modified,
and distributed without attribution. Attribution and this record are retained
voluntarily for provenance and reproducible imports.

Unreal imports the source into
`/Game/Props/FireExtinguisher/OpenGameArt_JamesWhite_2015` using
`Content/Python/import_fire_extinguisher_asset.py`.
