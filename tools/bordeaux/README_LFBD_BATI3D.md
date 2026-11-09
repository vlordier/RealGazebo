# LFBD Bati3D visual-context layer

This path turns Bordeaux Métropole's public, textured Bati3D 2012 archive into
a bounded RealGazebo visual layer around LFBD. It is deliberately separate
from airport geometry and must be presented as **historical context only**.

From the workspace root, after `BOD-CUAS make bati3d-core`:

```bash
python3 RealGazebo/tools/bordeaux/prepare_lfbd_bati3d_fbx.py \
  --index BOD-CUAS/data/lfbd/bati3d/index.json \
  --source-dir BOD-CUAS/data/lfbd/bati3d/extracted-core \
  --output-dir /private/tmp/lfbd_bati3d_core

LFBD_BATI3D_MANIFEST=/private/tmp/lfbd_bati3d_core/manifest.json \
  UnrealEditor-Cmd realgazebo-ue5.uproject \
  -ExecutePythonScript=RealGazebo/tools/bordeaux/import_lfbd_bati3d_ue.py
```

The importer maps EPSG:3945 easting/northing to Unreal X/Y in centimetres and
places LFBD `(1407200, 4187700)` at the world origin. Imported actors are
named `LFBD_Bati3D2012_xNNyNN` and tagged `HistoricalBati3D2012`,
`FuriaContextOnly`, and `NoOperationalAuthority`.

Do not use this data for navigation, clearance, obstacle avoidance, current
airport layout, or EO/IR evidence. It is a scenario backdrop only.

After import, run `capture_lfbd_bati3d_ue.py` through the regular UE editor to
produce `lfbd-bati3d-context.png`. It is a visual QA artifact, not simulated
sensor output.
