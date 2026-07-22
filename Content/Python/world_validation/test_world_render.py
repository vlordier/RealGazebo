import os
from pathlib import Path

import unreal

MAP = os.environ.get("REALGAZEBO_WORLD_MAP", "/RealGazebo/Maps/RealGazebo")
CAMERA = os.environ.get("REALGAZEBO_WORLD_CAMERA", "")
OUTPUT = Path(
    os.environ.get(
        "REALGAZEBO_WORLD_SCREENSHOT",
        str(Path(unreal.Paths.project_saved_dir()) / "WorldValidation" / "world.png"),
    )
)


def _class_name(obj):
    return obj.get_class().get_name()


def _label(actor):
    try:
        return actor.get_actor_label()
    except Exception:
        return actor.get_name()


def _choose_camera():
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cameras = [
        actor
        for actor in actor_subsystem.get_all_level_actors()
        if _class_name(actor) in {"CameraActor", "CineCameraActor"}
    ]
    if CAMERA:
        for camera in cameras:
            if _label(camera) == CAMERA or camera.get_name() == CAMERA:
                return camera
        raise RuntimeError(f"requested validation camera not found: {CAMERA}")
    return cameras[0] if cameras else None


@unreal.AutomationScheduler.add_latent_command
def load_world():
    loaded = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    if not loaded:
        raise RuntimeError(f"failed to load validation map: {MAP}")
    yield
    unreal.AutomationLibrary.finish_loading_before_screenshot()
    yield


@unreal.AutomationScheduler.add_latent_command
def capture_world():
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        OUTPUT.unlink()
    camera = _choose_camera()
    task = unreal.AutomationLibrary.take_high_res_screenshot(
        1920,
        1080,
        str(OUTPUT),
        camera=camera,
        mask_enabled=False,
        capture_hdr=False,
        delay=0.25,
        force_game_view=True,
    )
    if not task.is_valid_task():
        raise RuntimeError("Unreal rejected validation screenshot task")
    while not task.is_task_done():
        yield
    if not OUTPUT.is_file():
        raise RuntimeError(f"validation screenshot was not written: {OUTPUT}")
    if OUTPUT.stat().st_size < 1024:
        raise RuntimeError(f"validation screenshot is unexpectedly small: {OUTPUT.stat().st_size} bytes")
    unreal.log(f"RealGazebo validation screenshot: {OUTPUT}")
