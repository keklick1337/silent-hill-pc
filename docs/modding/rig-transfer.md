# Replacing a character model: rig_transfer

[Back to the guide](README.md)

[`tools/modding/rig_transfer/`](../../tools/modding/rig_transfer/) is a tool outside the game. It takes a character model of your own and binds it to the skeleton of a character of the game.

The game poses its own bones from its animation clips and draws `convert/models/.../<NAME>.glb` through the glb's skin. A replacement therefore needs exactly the skeleton of the character it replaces: the same joint names (`boneNN`), the same hierarchy and the same inverse bind matrices. The tool takes those from the imported glb and replaces the mesh and the materials. Animations, AI, hit boxes and sounds of the character are not touched.

## What is in the folder

| File | What it does |
| --- | --- |
| `rig_transfer.py` | fits the mesh, computes the weights, writes the glb and its manifest `<NAME>.model.json`; `--inspect NAME` prints the bones and parts of a skeleton |
| `fbx6_ascii.py` | reads ASCII FBX 6.x files (old 3ds Max exports), which Blender does not open |
| `targets.json` | the characters of the game by model file name: the fit mode and the table "body part → bone number" for Harry, every enemy and every boss |
| `tools/modding/rig_transfer/examples/gta_sa_cj.source.json` | an example of a source profile: the file, its axes, the bone names by role, the picture of each material. The model itself is not in the repository |
| `make_character_patches.py` | writes the `chara/<Name>/character.json` patches that point characters at the new glbs |
| `blender_preview.py` | shows a result beside the original inside Blender |

## Requirements

- `python3` with `numpy` (Blender's bundled Python has it too);
- the imported game data: `gamedata/convert/models/CHARA/*.glb` with their `*.model.json`, beside the game after its first start;
- a source model with its own skeleton and skin weights: ASCII FBX 6.x or a skinned `.glb` (any model Blender opens can be exported as glb). Blender is needed only to look at the result.

## Steps

1. **Write a source profile** (`*.source.json`): `file`; the model's `up` and `forward` axes; `roles`, the names of its bones for the roles `hips`, `chest`, `neck`, `head` and, with `l_` and `r_`, `clavicle`, `upperarm`, `forearm`, `hand`, `thigh`, `shin`, `foot`, `toes`; `textures`, the picture of each material. Start from the example profile.
2. **Targets** (`targets.json`) usually need no change. To see a skeleton:

   ```sh
   python3 -I tools/modding/rig_transfer/rig_transfer.py --models <gamedata>/convert/models/CHARA --inspect HERO
   ```

3. **Build**:

   ```sh
   python3 -I tools/modding/rig_transfer/rig_transfer.py --source my.source.json --source-file /path/model.fbx \
       --targets tools/modding/rig_transfer/targets.json --models <gamedata>/convert/models/CHARA \
       --out <mod>/convert/models/<mod> --texture-uri ../../textures/<mod>/ --only HERO DOG
   ```

   Put the pictures of the materials into `<mod>/convert/textures/<mod>/`.
4. **Write the character patches**:

   ```sh
   python3 -I tools/modding/rig_transfer/make_character_patches.py --chara gamedata/chara \
       --glbs <mod>/convert/models/<mod> --out <mod>/chara --names Harry
   ```

   (or `--categories CommonEnemies OtherworldEnemies Bosses`). A patch is one key, `{"model": "models/<folder>/<FILE>.glb"}`. The models may live in one library mod and the patches in mods that depend on it: a file is looked up in all active mods ([data.md](data.md#another-model-for-a-character-of-the-game)).
5. **Check**: `blender_preview.py` in Blender, then in the game the console command `editormodel chara <Name>` (the model viewer, [tools.md](tools.md#asset-panels)) and `spawn <Name>`.

## Fit modes

- **`rig`** — the target has body parts. Every bone of the source that has a role is laid along the target's bone (joint on joint), the torso by shoulders, hips and neck; head and feet are turned like the target's, and the soles are put at the height of the target's soles. The weights are the source's own, renamed by role. Dogs (the arms become front legs), birds (the arms become wings) and several bosses are fitted this way.
- **`chain`** — the body bends along a chain of bones (a worm, a tentacle); weights by the segments of the chain.
- **`blob`** — no correspondence (an insect, a slug): the body is fitted into the creature's bounding box and the weights are taken from the nearest point of the original mesh and smoothed.

## Whose proportions

In the mode `rig`, the target's key `proportions` decides:

- **`target`** (the default): the source's limbs are stretched onto the target's bones. The model gets the arm, leg and neck lengths and the shoulder width of the game's character. Good for bodies of another shape; a human on Harry's skeleton comes out with a long neck and narrow, low shoulders.
- **`source`** (Harry and the humanoid enemies in `targets.json`): the model keeps its own proportions. Parts are only turned; the glb gets a bind pose of its own and the mark `"proportions": "model"`, and the engine takes the bone offsets from that bind pose instead of the animation file ([assets.md](assets.md#character-models), "Own proportions"). The scale follows the height of the target's skeleton (`scale` sets it as a number, `scale_factor` multiplies it). The root bones stay where the target's are, and the body is moved so that the soles stand at the height of the target's soles.

What to expect from `source`: arms and legs of another length arrive at other points than the original's — a two-handed grip meets only roughly, a bent leg may miss the floor or sink into it by a few centimetres. The item in Harry's hand is drawn at the model's hand; hit boxes, collision, the first-person camera and the flashlight follow the game's skeleton.

## Limits

- Up to 4 bones per vertex (the rest are dropped and the weights normalised); the engine does not draw bones above 63.
- Normals are recomputed on the fitted mesh, so hard edges of the source are smoothed.
- Fingers and the face of the source follow the hand and the head. There is one hand for all of Harry's hand variants: a weapon stays on its bone, but the fingers do not grip it.
- Extra bones of the target (hair, sleeves, a boss's wings) are left without vertices.
- Hit boxes and the shadow are the original's, so they do not match the visible mesh of a very different body.
- The original's semi-transparency is not carried over: the material of the new mesh is opaque.
- A glb exported from Blender is not a character of the game: Blender's importer loses the scale of the bones in the bind pose. Write the file only with `rig_transfer.py`.

Whatever model you bind, publish it only if you may distribute it.
