# UE5.8 world/render validation

This tooling validates that the migrated RealGazebo world is not merely present as `.umap`/`.uasset` files but is actually configured as a renderable sensor world.

## Structural world audit

```bash
tools/world_validation/run_world_audit_mac.sh /path/to/HostProject.uproject
```

Default map: `/RealGazebo/Maps/RealGazebo`.

The audit loads the map through Unreal and writes `Saved/WorldValidation/world_audit.json` with:

- actor/component class inventory;
- DirectionalLight, SkyLight, SkyAtmosphere, VolumetricCloud and ExponentialHeightFog actors;
- PostProcessVolume inventory and whether one is unbound;
- Landscape/LandscapeStreamingProxy actors;
- cameras and static-mesh actors;
- static mesh and material assets referenced by loaded components;
- null mesh/material slots and references that fail Unreal asset existence checks.

The report validator fails on the minimum sensor-render contract: no actors/components, missing DirectionalLight/SkyLight/SkyAtmosphere/PostProcessVolume, no unbound post-process volume, missing referenced assets, or mesh components with no mesh.

## Deterministic render test

Enable Unreal's built-in `PythonAutomationTest` plugin in the host project, then run:

```bash
tools/world_validation/run_world_render_test_mac.sh /path/to/HostProject.uproject
```

This executes the plugin test:

`Editor.Python.RealGazebo.world_validation.test_world_render`

It loads the canonical map, waits for loading to finish using Unreal's latent automation scheduler, captures a 1920x1080 screenshot, and exports the Unreal automation report under `Saved/WorldValidation/AutomationReport`.

Set `REALGAZEBO_WORLD_CAMERA` to an actor label/name to force a specific deterministic camera. Without it the first CameraActor/CineCameraActor is used, or the active viewport if no camera actor exists.

## Comparing against RealGazebo 1.16

Run the structural audit against the old host project/map and the current UE5.8 host project/map, preserve both JSON reports, and compare:

- required environment actor counts;
- landscape presence;
- material/mesh counts;
- missing/null references;
- camera inventory;
- warnings.

Do not infer visual equivalence from Git history alone: maps and assets are binary, and UE migrations can preserve files while breaking material shaders, lighting, exposure or references.

## What this does not prove

A successful structural audit does not prove photometric equivalence or formal sensor realism. The render automation is the basis for adding reference-image comparison once a known-good UE5.8 baseline image is approved. At that point use Unreal Automation screenshot comparison or an external perceptual/SSIM gate rather than pixel-exact comparison.
