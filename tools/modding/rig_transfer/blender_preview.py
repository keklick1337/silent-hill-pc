# SPDX-License-Identifier: GPL-3.0-or-later
"""Blender side of rig_transfer: look at a result next to the original before trying it in the game.

Run inside Blender (Scripting tab, or `exec(open(".../blender_preview.py").read())`), then:

    preview(["<gamedata>/convert/models/CHARA/DOG.glb", "<mod>/convert/models/<mod>/DOG.glb"])
    pose_test("bone07", (0, 0, 60))     # turn a bone of every imported armature: do the right vertices follow?
    close_preview()                     # remove the preview scene and everything in it

Everything lives in its own scene ("rig_transfer_preview"); the scene that was open is left untouched and is shown
again by close_preview(). Blender's glTF importer cannot keep the rest scale of the game's bones, so a glb exported
from Blender is NOT a valid character file: use Blender to look, and rig_transfer.py to write.
"""

import bpy

SCENE = "rig_transfer_preview"
_previous = {}


def _scene():
    scene = bpy.data.scenes.get(SCENE)
    if scene is None:
        scene = bpy.data.scenes.new(SCENE)
    window = bpy.context.window
    if window.scene != scene:
        _previous["scene"] = window.scene.name
        window.scene = scene
    return scene


def _purge(scene):
    for obj in list(scene.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for coll in list(scene.collection.children):
        bpy.data.collections.remove(coll)
    for _ in range(3):
        bpy.data.orphans_purge(do_local_ids=True, do_linked_ids=False, do_recursive=True)


def preview(paths, spacing=1.2):
    """Imports the glbs in a row along X, each in a collection named after its file."""
    scene = _scene()
    _purge(scene)
    for i, path in enumerate(paths):
        before = set(scene.objects)
        bpy.ops.import_scene.gltf(filepath=path, disable_bone_shape=True)
        new = [o for o in scene.objects if o not in before]
        coll = bpy.data.collections.new(f"{i}:{bpy.path.basename(path)}")
        scene.collection.children.link(coll)
        for obj in new:
            for c in list(obj.users_collection):
                c.objects.unlink(obj)
            coll.objects.link(obj)
            if obj.parent is None:
                obj.location.x += i * spacing
    return [o.name for o in scene.objects]


def pose_test(bone, euler_degrees=(0, 0, 45)):
    """Turns `bone` of every armature of the preview (None: back to the rest pose)."""
    import math

    scene = _scene()
    for obj in scene.objects:
        if obj.type != "ARMATURE":
            continue
        for pb in obj.pose.bones:
            pb.rotation_mode = "XYZ"
            pb.rotation_euler = (0, 0, 0)
        if bone is not None and bone in obj.pose.bones:
            obj.pose.bones[bone].rotation_euler = tuple(math.radians(a) for a in euler_degrees)
    bpy.context.view_layer.update()


def close_preview():
    scene = bpy.data.scenes.get(SCENE)
    if scene is None:
        return
    _purge(scene)
    back = bpy.data.scenes.get(_previous.get("scene", ""))
    if back is None:
        back = next((s for s in bpy.data.scenes if s != scene), None)
    if back is not None:
        bpy.context.window.scene = back
        bpy.data.scenes.remove(scene)
