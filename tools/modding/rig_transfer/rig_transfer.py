#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""rig_transfer: binds a foreign character mesh to the skeleton of a Silent Hill PC character.

The game poses its own bones (the ANM clips) and draws convert/models/CHARA/<NAME>.glb through the glb's skin, so a
replacement model needs exactly the skeleton of the character it replaces: the same joint names (boneNN), the same
hierarchy and the same inverse bind matrices. This tool takes those from the imported glb untouched, and writes a
new glb with only the mesh replaced (or, for a model that keeps its own proportions, with a bind pose of its own):

  1. fit      the source mesh is brought into the target's rest pose:
               rig   -- the source has its own skeleton: its limbs are laid along the target's bones (every mapped
                        joint lands on the target joint), so elbows and knees bend where the game bends them;
                        with "proportions": "source" the limbs are only turned, not stretched: the model keeps its
                        own bone lengths, shoulders and neck at one scale, and the glb gets a rest pose of its own
                        (asset extras "proportions": "model") that the engine plays the target's clips on;
               blob  -- no usable correspondence (worms, tentacles, insects): the whole body is oriented, scaled
                        and centred into the creature's bounding box;
  2. weights  rig: the source's own skin weights, renamed bone by bone through the role map;
              blob: taken from the nearest surface point of the original mesh, then smoothed over the new mesh;
  3. write    the target glb's nodes, skin and inverse bind matrices byte for byte, plus the new mesh and materials.

Needs python3 with numpy (Blender's bundled Python has it too). Documentation: docs/modding/rig-transfer.md.

  python3 -I rig_transfer.py --source model.source.json --targets targets.json \
      --models <gamedata>/convert/models/CHARA --out <mod>/convert/models/<mod> --only HERO DOG
"""

import argparse
import json
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------------------------------------------------------------
# Roles: the body parts a source skeleton and a target skeleton are matched by.

SIDES = ("l", "r")
ROLE_PARENT = {"hips": None, "chest": "hips", "neck": "chest", "head": "neck"}
CHAIN_CHILD = {"neck": "head"}
for _s in SIDES:
    ROLE_PARENT.update({
        f"{_s}_clavicle": "chest", f"{_s}_upperarm": f"{_s}_clavicle", f"{_s}_forearm": f"{_s}_upperarm",
        f"{_s}_hand": f"{_s}_forearm", f"{_s}_thigh": "hips", f"{_s}_shin": f"{_s}_thigh",
        f"{_s}_foot": f"{_s}_shin", f"{_s}_toes": f"{_s}_foot",
    })
    CHAIN_CHILD.update({
        f"{_s}_clavicle": f"{_s}_upperarm", f"{_s}_upperarm": f"{_s}_forearm", f"{_s}_forearm": f"{_s}_hand",
        f"{_s}_thigh": f"{_s}_shin", f"{_s}_shin": f"{_s}_foot", f"{_s}_foot": f"{_s}_toes",
    })
ROLE_ORDER = ["hips", "chest", "neck", "head"] + [
    f"{s}_{part}" for s in SIDES
    for part in ("clavicle", "upperarm", "forearm", "hand", "thigh", "shin", "foot", "toes")
]
TORSO = ("hips", "chest")

# ---------------------------------------------------------------------------------------------------------------------
# Small maths.


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-12 else np.zeros_like(v)


def swing(a, b):
    """The smallest rotation taking direction a onto direction b."""
    a, b = unit(a), unit(b)
    if not a.any() or not b.any():
        return np.eye(3)
    c = float(np.dot(a, b))
    axis = np.cross(a, b)
    s = np.linalg.norm(axis)
    if s < 1e-9:
        if c > 0:
            return np.eye(3)
        other = unit(np.cross(a, [1.0, 0, 0]) if abs(a[0]) < 0.9 else np.cross(a, [0, 1.0, 0]))
        return 2.0 * np.outer(other, other) - np.eye(3)
    k = axis / s
    kx = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + s * kx + (1.0 - c) * (kx @ kx)


def rotation_about(axis, degrees):
    return swing_axis_angle(unit(np.asarray(axis, dtype=np.float64)), np.radians(degrees))


def swing_axis_angle(k, angle):
    kx = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    return np.eye(3) + np.sin(angle) * kx + (1.0 - np.cos(angle)) * (kx @ kx)


def long_axis(points, origin):
    """The longest principal axis of a part, pointing away from `origin` (the part's joint)."""
    centre = points.mean(0)
    _, _, vt = np.linalg.svd(points - centre, full_matrices=False)
    axis = vt[0]
    return axis if np.dot(axis, centre - origin) >= 0 else -axis


def frame(lateral, spine):
    """An orthonormal frame (columns x, y, z) from a left-right and a hips-to-neck direction; None when degenerate."""
    x = unit(lateral)
    y = unit(spine - np.dot(spine, x) * x)
    if not x.any() or not y.any():
        return None
    return np.stack([x, y, np.cross(x, y)], axis=1)


def axis_vector(text):
    table = {"x": (1, 0, 0), "y": (0, 1, 0), "z": (0, 0, 1)}
    if isinstance(text, (list, tuple)):
        return unit(np.array(text, dtype=np.float64))
    sign = -1.0 if text.startswith("-") else 1.0
    return sign * np.array(table[text.lstrip("+-").lower()], dtype=np.float64)


def vertex_normals(positions, triangles):
    n = np.zeros_like(positions)
    a, b, c = positions[triangles[:, 0]], positions[triangles[:, 1]], positions[triangles[:, 2]]
    face = np.cross(b - a, c - a)
    for k in range(3):
        np.add.at(n, triangles[:, k], face)
    length = np.linalg.norm(n, axis=1, keepdims=True)
    return np.where(length > 1e-12, n / np.maximum(length, 1e-12), np.array([0.0, 1.0, 0.0]))


def closest_on_triangles(points, a, b, c):
    """For every point the index of the nearest triangle and the barycentric weights of the nearest point on it."""
    best_d = np.full(len(points), np.inf)
    best_t = np.zeros(len(points), dtype=np.int64)
    best_w = np.zeros((len(points), 3))
    ab, ac = b - a, c - a
    for start in range(0, len(points), 256):
        p = points[start:start + 256, None, :]
        ap = p - a
        d1, d2 = (ab * ap).sum(-1), (ac * ap).sum(-1)
        bp = p - b
        d3, d4 = (ab * bp).sum(-1), (ac * bp).sum(-1)
        cp = p - c
        d5, d6 = (ab * cp).sum(-1), (ac * cp).sum(-1)
        va, vb, vc = d3 * d6 - d5 * d4, d5 * d2 - d1 * d6, d1 * d4 - d3 * d2
        denom = va + vb + vc
        denom = np.where(np.abs(denom) < 1e-20, 1e-20, denom)
        v, w = vb / denom, vc / denom
        u = 1.0 - v - w

        def edge(num, den):
            return np.clip(num / np.where(np.abs(den) < 1e-20, 1e-20, den), 0.0, 1.0)

        # Regions of the triangle's Voronoi diagram (Ericson, Real-Time Collision Detection, 5.1.5).
        t_ab, t_ac, t_bc = edge(d1, d1 - d3), edge(d2, d2 - d6), edge(d4 - d3, (d4 - d3) + (d5 - d6))
        zero, one = np.zeros_like(u), np.ones_like(u)
        bary = np.stack([u, v, w], -1)
        inside = (va >= 0) & (vb >= 0) & (vc >= 0)
        cases = [
            ((d1 <= 0) & (d2 <= 0), np.stack([one, zero, zero], -1)),
            ((d3 >= 0) & (d4 <= d3), np.stack([zero, one, zero], -1)),
            ((d6 >= 0) & (d5 <= d6), np.stack([zero, zero, one], -1)),
            ((vc <= 0) & (d1 >= 0) & (d3 <= 0), np.stack([1 - t_ab, t_ab, zero], -1)),
            ((vb <= 0) & (d2 >= 0) & (d6 <= 0), np.stack([1 - t_ac, zero, t_ac], -1)),
            ((va <= 0) & ((d4 - d3) >= 0) & ((d5 - d6) >= 0), np.stack([zero, 1 - t_bc, t_bc], -1)),
        ]
        done = inside.copy()
        for mask, value in cases:
            take = mask & ~done
            bary = np.where(take[..., None], value, bary)
            done |= take
        nearest = bary[..., 0:1] * a + bary[..., 1:2] * b + bary[..., 2:3] * c
        dist = ((nearest - p) ** 2).sum(-1)
        t = dist.argmin(1)
        rows = np.arange(len(t))
        best_d[start:start + 256] = dist[rows, t]
        best_t[start:start + 256] = t
        best_w[start:start + 256] = bary[rows, t]
    return best_t, best_w


# ---------------------------------------------------------------------------------------------------------------------
# glb reading.

_COMPONENT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, _version, _length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError(f"{path}: not a glb")
    json_len, _ = struct.unpack_from("<II", data, 12)
    doc = json.loads(data[20:20 + json_len])
    rest = 20 + json_len
    binary = b""
    if rest + 8 <= len(data):
        bin_len, _ = struct.unpack_from("<II", data, rest)
        binary = data[rest + 8:rest + 8 + bin_len]
    return doc, binary


def accessor(doc, binary, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    dtype = np.dtype(_COMPONENT[acc["componentType"]])
    width = _WIDTH[acc["type"]]
    offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride", 0)
    count = acc["count"]
    if stride and stride != dtype.itemsize * width:
        out = np.zeros((count, width), dtype=dtype)
        for i in range(count):
            out[i] = np.frombuffer(binary, dtype=dtype, count=width, offset=offset + i * stride)
    else:
        out = np.frombuffer(binary, dtype=dtype, count=count * width, offset=offset).reshape(count, width)
    if acc.get("normalized") and dtype.kind in "ui":
        out = out.astype(np.float64) / np.iinfo(dtype).max
    return out


def bone_of_joint_name(name):
    """The game bone of a joint name ("bone07", "bone_7"), or -1 -- the engine's rule (skin_model.cpp)."""
    lower = (name or "").lower()
    if not lower.startswith("bone"):
        return -1
    digits = lower[4:].lstrip("_") if lower[4:5] == "_" else lower[4:]
    return int(digits) if digits.isdigit() and len(digits) <= 3 and int(digits) < 256 else -1


class Target:
    """The skeleton and the original mesh of an imported character glb."""

    def __init__(self, path):
        self.path = path
        self.doc, self.binary = read_glb(path)
        doc = self.doc
        if not doc.get("skins"):
            raise ValueError(f"{path}: no skin")
        skin = doc["skins"][0]
        self.joint_nodes = skin["joints"]
        ibm = accessor(doc, self.binary, skin["inverseBindMatrices"]).astype(np.float64)
        self.joint_bone = []
        self.bone_world = {}
        for j, node_index in enumerate(self.joint_nodes):
            node = doc["nodes"][node_index]
            bone = bone_of_joint_name(node.get("name"))
            if bone < 0:
                bone = int(node.get("extras", {}).get("bone", -1))
            self.joint_bone.append(bone)
            if bone >= 0:
                # glTF matrices are column-major.
                self.bone_world[bone] = np.linalg.inv(ibm[j].reshape(4, 4).T)
        self.bone_joint = {b: j for j, b in enumerate(self.joint_bone) if b >= 0}
        parent_node = {}
        for i, node in enumerate(doc["nodes"]):
            for c in node.get("children", []):
                parent_node[c] = i
        node_joint = {n: j for j, n in enumerate(self.joint_nodes)}
        self.bone_parent = {}
        for j, node_index in enumerate(self.joint_nodes):
            p = parent_node.get(node_index)
            while p is not None and (p not in node_joint or self.joint_bone[node_joint[p]] < 0):
                p = parent_node.get(p)
            self.bone_parent[self.joint_bone[j]] = self.joint_bone[node_joint[p]] if p is not None else -1
        # The bones whose translation the clips write (the root bones): the importer names the track in the joint's
        # extras; a file without it counts the bones that hang from the root.
        self.bone_animated = {}
        for j, node_index in enumerate(self.joint_nodes):
            bone = self.joint_bone[j]
            slot = doc["nodes"][node_index].get("extras", {}).get("translation_slot")
            self.bone_animated[bone] = (slot >= 0) if slot is not None else self.bone_parent[bone] <= 0

        positions, bones, triangles, part_names = [], [], [], []
        base = 0
        for node in doc["nodes"]:
            if "mesh" not in node or node.get("skin") != 0:
                continue
            for prim in doc["meshes"][node["mesh"]]["primitives"]:
                attr = prim["attributes"]
                pos = accessor(doc, self.binary, attr["POSITION"]).astype(np.float64)
                joints = accessor(doc, self.binary, attr["JOINTS_0"]).astype(np.int64)
                weights = accessor(doc, self.binary, attr["WEIGHTS_0"]).astype(np.float64)
                dominant = joints[np.arange(len(pos)), weights.argmax(1)]
                idx = accessor(doc, self.binary, prim["indices"]).astype(np.int64).reshape(-1, 3)
                positions.append(pos)
                bones.append(np.array([self.joint_bone[j] for j in dominant], dtype=np.int64))
                triangles.append(idx + base)
                part_names.append(node.get("name", ""))
                base += len(pos)
        self.positions = np.concatenate(positions) if positions else np.zeros((0, 3))
        self.vertex_bone = np.concatenate(bones) if bones else np.zeros(0, dtype=np.int64)
        self.triangles = np.concatenate(triangles) if triangles else np.zeros((0, 3), dtype=np.int64)
        self.part_names = part_names

    def bone_position(self, bone):
        return self.bone_world[bone][:3, 3]

    def part_vertices(self, bone):
        return self.positions[self.vertex_bone == bone]


# ---------------------------------------------------------------------------------------------------------------------
# The source model.


class Source:
    """A skinned mesh in the tool's space: +Y up, facing +Z (the game's glTF convention), arbitrary units.

    positions (n,3); corners (m,) vertex per output vertex; uvs (m,2), V down; triangles (t,3) over the output
    vertices; triangle_material (t,); materials [{"name", "image"}]; bones {name: {"parent", "position"}};
    weights (n, bones) dense, rows summing to one.
    """

    def __init__(self, profile_path, file_override=None):
        with open(profile_path, "r", encoding="utf-8") as f:
            self.profile = json.load(f)
        self.folder = os.path.dirname(os.path.abspath(profile_path))
        file = file_override or self.profile["file"]
        if not os.path.isabs(file):
            file = os.path.join(self.folder, file)
        if file.lower().endswith(".fbx"):
            self._from_fbx(file)
        elif file.lower().endswith(".glb"):
            self._from_glb(file)
        else:
            raise ValueError(f"{file}: the source is an ASCII FBX 6.x or a skinned .glb")
        self._to_tool_space()
        self.roles = {role: bone for role, bone in self.profile.get("roles", {}).items() if not role.startswith("_")}
        for role, bone in self.roles.items():
            if role not in ROLE_PARENT:
                raise ValueError(f"source profile: unknown role {role!r}")
            if bone not in self.bone_names:
                raise ValueError(f"source profile: role {role}: the source has no bone {bone!r}")
        if self.roles:
            self._drop_roleless_weights()

    def _drop_roleless_weights(self):
        """A bone above every role (a ground or root node some exporters weight the trouser hems to) has no body
        part to follow: its share goes to the vertex's other bones, or to the nearest vertex that has some."""
        role = self.bone_role(set(self.roles))
        loose = np.array([role[n] is None for n in self.bone_names])
        if not loose.any() or not self.weights[:, loose].any():
            return
        kept = self.weights.copy()
        kept[:, loose] = 0.0
        total = kept.sum(1)
        empty = total < 1e-6
        if empty.all():
            return
        removed = 0
        if empty.any():
            # A triangle wholly on such a bone is no body part (a filler between the trouser legs): it goes.
            tri = self.corners[self.triangles]
            filler = empty[tri].all(1)
            removed = int(filler.sum())
            self.triangles = self.triangles[~filler]
            self.triangle_material = self.triangle_material[~filler]
            # The rest take the weights of the vertices they are joined to, ring by ring.
            tri = self.corners[self.triangles]
            edges = np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]])
            edges = np.concatenate([edges, edges[:, ::-1]])
            for _ in range(16):
                todo = kept.sum(1) < 1e-6
                if not todo.any():
                    break
                use = todo[edges[:, 0]] & ~todo[edges[:, 1]]
                if not use.any():
                    break
                add = np.zeros_like(kept)
                np.add.at(add, edges[use, 0], kept[edges[use, 1]])
                kept += add
            good = np.flatnonzero(kept.sum(1) >= 1e-6)
            for v in np.flatnonzero(kept.sum(1) < 1e-6):
                nearest = good[((self.positions[good] - self.positions[v]) ** 2).sum(1).argmin()]
                kept[v] = kept[nearest]
            total = kept.sum(1)
        moved = int((self.weights[:, loose].sum(1) > 1e-6).sum())
        print(f"  note: {moved} vertices were weighted to bones above every role "
              f"({', '.join(n for n, l in zip(self.bone_names, loose) if l)}); they follow their neighbours"
              f"{f', {removed} triangles wholly on those bones were removed' if removed else ''}")
        self.weights = kept / total[:, None]

    def _from_fbx(self, file):
        import fbx6_ascii

        m = fbx6_ascii.load(file)
        self.positions = m["positions"]
        names = [n for n, b in m["bones"].items() if b["world"] is not None]
        self.bone_names = names
        self.bone_parent = {}
        for n in names:
            p = m["bones"][n]["parent"]
            while p is not None and m["bones"][p]["world"] is None:
                p = m["bones"][p]["parent"]
            self.bone_parent[n] = p
        self.bone_position = {n: m["bones"][n]["world"][:3, 3].copy() for n in names}
        w = np.zeros((len(self.positions), len(names)))
        for n, (idx, val) in m["weights"].items():
            w[idx, names.index(n)] += val
        self._set_weights(w)
        corner_vertex = np.array([i for p in m["polygons"] for i in p], dtype=np.int64)
        uvs = m["corner_uvs"] if m["corner_uvs"] is not None else np.zeros((len(corner_vertex), 2))
        uvs = np.stack([uvs[:, 0], 1.0 - uvs[:, 1]], axis=1)  # FBX has V up
        # One output vertex per distinct (vertex, uv).
        key = {}
        out_vertex, out_uv, corner_out = [], [], []
        for v, uv in zip(corner_vertex, uvs):
            k = (int(v), round(float(uv[0]), 6), round(float(uv[1]), 6))
            if k not in key:
                key[k] = len(out_vertex)
                out_vertex.append(int(v))
                out_uv.append(uv)
            corner_out.append(key[k])
        triangles, materials = [], []
        at = 0
        for p, mat in zip(m["polygons"], m["polygon_material"]):
            for k in range(1, len(p) - 1):
                triangles.append((corner_out[at], corner_out[at + k], corner_out[at + k + 1]))
                materials.append(int(mat))
            at += len(p)
        self.corners = np.array(out_vertex, dtype=np.int64)
        self.uvs = np.array(out_uv, dtype=np.float64)
        self.triangles = np.array(triangles, dtype=np.int64)
        self.triangle_material = np.array(materials, dtype=np.int64)
        self.material_names = list(m["materials"]) or ["material"]

    def _from_glb(self, file):
        doc, binary = read_glb(file)
        skin = doc["skins"][0]
        ibm = accessor(doc, binary, skin["inverseBindMatrices"]).astype(np.float64)
        nodes = doc["nodes"]
        parent_node = {c: i for i, n in enumerate(nodes) for c in n.get("children", [])}
        joint_of = {n: j for j, n in enumerate(skin["joints"])}
        names = [nodes[n].get("name", f"joint{j}") for j, n in enumerate(skin["joints"])]
        self.bone_names = names
        self.bone_parent, self.bone_position = {}, {}
        for j, n in enumerate(skin["joints"]):
            p = parent_node.get(n)
            while p is not None and p not in joint_of:
                p = parent_node.get(p)
            self.bone_parent[names[j]] = names[joint_of[p]] if p is not None else None
            self.bone_position[names[j]] = np.linalg.inv(ibm[j].reshape(4, 4).T)[:3, 3]
        positions, uvs, weights, triangles, materials = [], [], [], [], []
        base = 0
        for node in nodes:
            if "mesh" not in node or "skin" not in node:
                continue
            for prim in doc["meshes"][node["mesh"]]["primitives"]:
                attr = prim["attributes"]
                pos = accessor(doc, binary, attr["POSITION"]).astype(np.float64)
                uv = accessor(doc, binary, attr["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in attr else np.zeros((len(pos), 2))
                joints = accessor(doc, binary, attr["JOINTS_0"]).astype(np.int64)
                wts = accessor(doc, binary, attr["WEIGHTS_0"]).astype(np.float64)
                w = np.zeros((len(pos), len(names)))
                for k in range(4):
                    np.add.at(w, (np.arange(len(pos)), joints[:, k]), wts[:, k])
                idx = accessor(doc, binary, prim["indices"]).astype(np.int64).reshape(-1, 3)
                positions.append(pos), uvs.append(uv), weights.append(w)
                triangles.append(idx + base)
                materials.append(np.full(len(idx), prim.get("material", 0), dtype=np.int64))
                base += len(pos)
        self.positions = np.concatenate(positions)
        self.corners = np.arange(len(self.positions))
        self.uvs = np.concatenate(uvs)
        self.triangles = np.concatenate(triangles)
        self.triangle_material = np.concatenate(materials)
        self.material_names = [m.get("name", f"material{i}") for i, m in enumerate(doc.get("materials", [{}]))]
        self._set_weights(np.concatenate(weights))

    def _set_weights(self, w):
        total = w.sum(1, keepdims=True)
        loose = total[:, 0] < 1e-6
        if loose.any():
            print(f"  note: {int(loose.sum())} source vertices have no weights; bound to the first bone")
            w[loose, 0] = 1.0
            total = w.sum(1, keepdims=True)
        self.weights = w / total

    def _to_tool_space(self):
        up = axis_vector(self.profile.get("up", "+y"))
        forward = axis_vector(self.profile.get("forward", "+z"))
        right = np.cross(up, forward)  # tool space: x = up x forward for (+Y up, +Z forward)
        basis = np.stack([right, up, forward])  # rows: tool axes in source coordinates
        self.positions = self.positions @ basis.T
        for n in self.bone_names:
            self.bone_position[n] = basis @ self.bone_position[n]
        if np.linalg.det(basis) < 0:
            self.triangles = self.triangles[:, ::-1].copy()

    def bone_role(self, present):
        """For every source bone the nearest role, at or above it, that `present` holds; None when there is none."""
        role_of_bone = {bone: role for role, bone in self.roles.items()}
        out = {}
        for name in self.bone_names:
            b, role = name, None
            while b is not None and role is None:
                role = role_of_bone.get(b)
                b = self.bone_parent[b]
            while role is not None and role not in present:
                role = ROLE_PARENT[role]
            out[name] = role
        return out

    def posed(self, pose):
        """Positions and joint positions after rotating role subtrees: pose = {role: [axis xyz, degrees]}."""
        if not pose:
            return self.positions, dict(self.bone_position)
        roles = set(self.roles)
        role_of = self.bone_role(roles)
        matrices = {}

        def matrix_of(role):
            if role is None:
                return np.eye(4)
            if role in matrices:
                return matrices[role]
            parent = ROLE_PARENT[role]
            while parent is not None and parent not in roles:
                parent = ROLE_PARENT[parent]
            m = matrix_of(parent)
            if role in pose:
                axis, degrees = pose[role]
                pivot = self.bone_position[self.roles[role]]
                local = np.eye(4)
                local[:3, :3] = rotation_about(axis, degrees)
                local[:3, 3] = pivot - local[:3, :3] @ pivot
                m = m @ local
            matrices[role] = m
            return m

        per_bone = np.stack([matrix_of(role_of[n]) for n in self.bone_names])
        hom = np.concatenate([self.positions, np.ones((len(self.positions), 1))], axis=1)
        moved = np.einsum("vb,bij,vj->vi", self.weights, per_bone, hom)[:, :3]
        joints = {}
        for i, n in enumerate(self.bone_names):
            joints[n] = (per_bone[i] @ np.append(self.bone_position[n], 1.0))[:3]
        return moved, joints


# ---------------------------------------------------------------------------------------------------------------------
# Fitting.


class Fit:
    def __init__(self, positions, bone_weights, notes):
        self.positions = positions        # (n,3) in the target's bind space (glTF axes, metres)
        self.bone_weights = bone_weights  # (n, k) dense over `bones`
        self.bones = None                 # the game bone of every column
        self.notes = notes
        self.bind = None                  # own proportions: {game bone: 4x4 bind matrix}, else the target's own


def fit_rig(source, target, spec):
    notes = []
    troles = {r: int(b) for r, b in spec["roles"].items() if not r.startswith("_")}
    for role, bone in troles.items():
        if role not in ROLE_PARENT:
            raise ValueError(f"target roles: unknown role {role!r}")
        if bone not in target.bone_world:
            raise ValueError(f"target roles: {role}: bone {bone} is not in the skeleton")
    positions, joints = source.posed(spec.get("pose"))
    spos = {role: joints[bone] for role, bone in source.roles.items()}
    present = {r for r in troles if r in source.roles}
    tpos = {r: target.bone_position(troles[r]) for r in present}

    # Left and right must mean the same side on both models, else every limb would cross the body.
    if spec.get("swap_sides"):
        flipped = {}
        for r in present:
            other = ("r" if r[0] == "l" else "l") + r[1:] if r[:2] in ("l_", "r_") else r
            flipped[r] = other
        troles = {flipped[r]: troles[r] for r in present}
        present = set(troles)
        tpos = {r: target.bone_position(troles[r]) for r in present}
        notes.append("sides swapped")

    # A role that shares its bone with the next role of its chain (the neck on the head's bone) joins it: the
    # joined role pivots where the first one starts.
    pivot_role = {r: r for r in present}
    alias = {}
    for r in ROLE_ORDER:
        c = CHAIN_CHILD.get(r)
        if r in present and c in present and troles[r] == troles[c]:
            alias[r] = c
            pivot_role[c] = pivot_role[r]
    active = [r for r in ROLE_ORDER if r in present and r not in alias]

    def chain_child(role):
        c = CHAIN_CHILD.get(role)
        while c is not None and (c not in present or c in alias):
            c = CHAIN_CHILD.get(c)
        return c

    def parent_active(role):
        p = ROLE_PARENT[role]
        while p is not None and p not in active:
            p = ROLE_PARENT[p]
        return p

    # The size of the target against the source: the median length ratio of the limb segments.
    ratios = []
    for r in active:
        c = chain_child(r)
        if r in TORSO or c is None:
            continue
        ls = np.linalg.norm(spos[c] - spos[pivot_role[r]])
        lt = np.linalg.norm(tpos[c] - tpos[r])
        if ls > 1e-6 and lt > 1e-6:
            ratios.append(lt / ls)
    if spec.get("girth") is not None:
        girth = float(spec["girth"])
    elif ratios:
        girth = float(np.median(ratios))
    else:
        girth = float(np.ptp(target.positions, axis=0).max() / max(np.ptp(positions, axis=0).max(), 1e-9))
    girth *= float(spec.get("girth_scale", 1.0))
    notes.append(f"scale {girth:.4f}")

    role_of_bone = source.bone_role(present)
    for n in role_of_bone:
        role_of_bone[n] = alias.get(role_of_bone[n], role_of_bone[n])
    dominant = source.weights.argmax(1)
    vertex_role = np.array([role_of_bone[source.bone_names[b]] or "" for b in dominant])

    def first(*roles):
        for r in roles:
            if r in present:
                return r
        return None

    spine_top = first("neck", "head")
    clamp = spec.get("clamp", [0.6, 1.7])
    aim_ends = spec.get("aim_ends", ("head", "foot", "toes"))
    reach_ends = spec.get("reach_ends", ("foot", "toes"))

    def end_rotation(r, r_parent, ps, pt):
        """An end part (head, hand, foot) is turned like the target's part."""
        kind = r.split("_")[-1]
        mine = positions[vertex_role == r]
        theirs = target.part_vertices(troles[r])
        if len(mine) >= 3 and len(theirs) >= 3 and kind in aim_ends:
            if kind == "head":
                cs, ct = mine.mean(0) - ps, theirs.mean(0) - pt
            else:
                cs, ct = long_axis(mine, ps), long_axis(theirs, target.bone_position(troles[r]))
            if np.linalg.norm(cs) > 1e-6 and np.linalg.norm(ct) > 1e-6:
                return swing(r_parent @ cs, ct) @ r_parent
        return r_parent

    def own_proportions():
        """The source keeps its own bone lengths. Every part is only turned, never stretched: the result is the
        source in its own pose at one scale, with a bind pose of its own -- each target bone sits on the source's
        joint and is turned back by what the part needs to lie along the target's bone. The engine plays the
        target's clips on those joints (the glb says "proportions": "model")."""
        def stature(pos, pivot):
            legs = []
            for s in SIDES:
                chain = [f"{s}_thigh", f"{s}_shin", f"{s}_foot"]
                if all(c in present for c in chain):
                    a, b, c = (pos[pivot(x)] for x in chain)
                    legs.append(np.linalg.norm(b - a) + np.linalg.norm(c - b))
            if not legs or "hips" not in present or spine_top is None or "l_thigh" not in present or \
                    "r_thigh" not in present:
                return None
            hips = pos[pivot("hips")]
            between = (pos[pivot("l_thigh")] + pos[pivot("r_thigh")]) / 2
            return float(np.mean(legs) + np.linalg.norm(hips - between) + np.linalg.norm(pos[pivot(spine_top)] - hips))

        scale = spec.get("scale")
        if scale is None:
            st, ss = stature(tpos, lambda r: r), stature(spos, lambda r: pivot_role.get(r, r))
            scale = st / ss if st and ss else girth
        scale = float(scale) * float(spec.get("scale_factor", 1.0))
        notes[:] = [n for n in notes if not n.startswith("scale ")] + [f"own proportions, scale {scale:.4f}"]

        rotation = {}
        for r in active:
            parent = parent_active(r)
            r_parent = rotation[parent] if parent is not None else np.eye(3)
            ps, pt = spos[pivot_role[r]], tpos[r]
            child = chain_child(r)
            rot = r_parent
            if r in TORSO:
                lr = ("l_thigh", "r_thigh") if r == "hips" else ("l_upperarm", "r_upperarm")
                if lr[0] in present and lr[1] in present and spine_top is not None:
                    fs = frame(spos[lr[0]] - spos[lr[1]], spos[pivot_role.get(spine_top, spine_top)] - ps)
                    ft = frame(tpos[lr[0]] - tpos[lr[1]], tpos[spine_top] - pt)
                    if fs is not None and ft is not None:
                        rot = ft @ fs.T
            elif child is not None and np.linalg.norm(tpos[child] - pt) > 1e-6 and \
                    np.linalg.norm(spos[child] - ps) > 1e-9:
                rot = swing(r_parent @ (spos[child] - ps), tpos[child] - pt) @ r_parent
            else:
                rot = end_rotation(r, r_parent, ps, pt)
            rotation[r] = rot

        root = active[0]
        pinned = {r: parent_active(r) is None or target.bone_animated.get(troles[r], False) for r in active}

        def place(lift):
            """Where every part's joint is: in the source (pivot_s) and in the target's rest pose (pivot_t). A bone
            the clips move stays where the target has it; the others are where the source's own joints come to."""
            pivot_s, pivot_t = {}, {}
            for r in active:
                parent = parent_active(r)
                if parent is None:
                    pivot_s[r] = spos[pivot_role[r]] - rotation[r].T @ lift / scale
                    pivot_t[r] = tpos[r]
                elif pinned[r]:
                    pivot_t[r] = tpos[r]
                    pivot_s[r] = pivot_s[parent] + rotation[parent].T @ (tpos[r] - pivot_t[parent]) / scale
                else:
                    pivot_s[r] = spos[pivot_role[r]]
                    pivot_t[r] = pivot_t[parent] + scale * rotation[parent] @ (pivot_s[r] - pivot_s[parent])
            return pivot_s, pivot_t

        # The soles go where the target's soles are: the whole body moves up or down against the root bones.
        lift = np.zeros(3)
        pivot_s, pivot_t = place(lift)
        low_s, low_t = [], []
        for r in active:
            if r.split("_")[-1] not in ("foot", "toes"):
                continue
            mine = positions[vertex_role == r]
            theirs = target.part_vertices(troles[r])
            if len(mine) and len(theirs):
                low_s.append((scale * (mine - pivot_s[r]) @ rotation[r].T + pivot_t[r])[:, 1].min())
                low_t.append(theirs[:, 1].min())
        if low_s:
            limit = 0.25 * scale * max(np.ptp(positions[:, 1]), 1e-9)
            lift = np.array([0.0, float(np.clip(min(low_t) - min(low_s), -limit, limit)), 0.0])
            pivot_s, pivot_t = place(lift)
            notes.append(f"soles moved {lift[1]:+.3f} m")

        def out_space(x):
            return scale * (x - pivot_s[root]) + pivot_t[root]

        # The bind pose: a bone with a part sits on the part's joint, turned back by the part's turn; a bone without
        # one keeps its place against its parent.
        role_of_target = {}
        for r in active:
            role_of_target.setdefault(troles[r], r)
        bind, turn = {}, {}
        todo = sorted(target.bone_world)
        for _ in range(len(todo) + 1):
            for b in todo:
                p = target.bone_parent.get(b, -1)
                if b in bind or (p in target.bone_world and p not in bind and b not in role_of_target):
                    continue
                m = target.bone_world[b].copy()
                if b in role_of_target:
                    r = role_of_target[b]
                    turn[b] = rotation[r]
                    m[:3, 3] = out_space(pivot_s[r])
                elif p in bind:
                    turn[b] = turn[p]
                    m[:3, 3] = bind[p][:3, 3] + turn[p].T @ (target.bone_position(b) - target.bone_position(p))
                else:
                    turn[b] = np.eye(3)
                m[:3, :3] = turn[b].T @ m[:3, :3]
                bind[b] = m

        bones = sorted({troles[r] for r in active})
        column = {b: i for i, b in enumerate(bones)}
        weights = np.zeros((len(positions), len(bones)))
        fallback = "hips" if "hips" in active else active[0]
        for b, name in enumerate(source.bone_names):
            r = role_of_bone[name] or fallback
            weights[:, column[troles[r]]] += source.weights[:, b]
        fit = Fit(out_space(positions), weights, notes)
        fit.bones = bones
        fit.bind = bind
        return fit

    if spec.get("proportions", "target") == "source":
        return own_proportions()
    if spec.get("proportions", "target") != "target":
        raise ValueError("proportions is \"target\" (the default) or \"source\"")

    def solve(tpos, reach):
        """The transform of every active role; `reach` collects how far each end part has to move along its limb
        for its far end to be where the target part's far end is (None: not looked for)."""
        rotation, linear, pivot_s, pivot_t = {}, {}, {}, {}
        for r in active:
            parent = parent_active(r)
            r_parent = rotation[parent] if parent is not None else np.eye(3)
            ps, pt = spos[pivot_role[r]], tpos[r]
            pivot_s[r], pivot_t[r] = ps, pt
            child = chain_child(r)
            if r in TORSO:
                lr = ("l_thigh", "r_thigh") if r == "hips" else ("l_upperarm", "r_upperarm")
                fs = ft = None
                if lr[0] in present and lr[1] in present and spine_top is not None:
                    top_s, top_t = spos[pivot_role.get(spine_top, spine_top)], tpos[spine_top]
                    fs = frame(spos[lr[0]] - spos[lr[1]], top_s - ps)
                    ft = frame(tpos[lr[0]] - tpos[lr[1]], top_t - pt)
                if fs is None or ft is None:
                    rot = r_parent
                    scale = girth * np.eye(3)
                else:
                    rot = ft @ fs.T
                    sa = np.linalg.norm(tpos[lr[0]] - tpos[lr[1]]) / max(np.linalg.norm(spos[lr[0]] - spos[lr[1]]), 1e-9)
                    sb = girth
                    if r == "chest":
                        sb = np.linalg.norm(top_t - pt) / max(np.linalg.norm(top_s - ps), 1e-9)
                    sa = float(np.clip(sa, clamp[0] * girth, clamp[1] * girth))
                    sb = float(np.clip(sb, clamp[0] * girth, clamp[1] * girth))
                    x, y, z = fs[:, 0], fs[:, 1], fs[:, 2]
                    scale = sa * np.outer(x, x) + sb * np.outer(y, y) + girth * np.outer(z, z)
            elif child is not None and np.linalg.norm(tpos[child] - pt) > 1e-6:
                ds, dt = spos[child] - ps, tpos[child] - pt
                rot = swing(r_parent @ ds, dt) @ r_parent
                d = unit(ds)
                length = np.linalg.norm(dt) / max(np.linalg.norm(ds), 1e-9)
                scale = girth * np.eye(3) + (length - girth) * np.outer(d, d)
            else:
                # An end part (head, hand, foot): it keeps its shape and is turned like the target's part.
                rot = r_parent
                size = girth
                kind = r.split("_")[-1]
                mine = positions[vertex_role == r]
                theirs = target.part_vertices(troles[r])
                if len(mine) >= 3 and len(theirs) >= 3 and kind in aim_ends:
                    if kind == "head":
                        cs, ct = mine.mean(0) - ps, theirs.mean(0) - pt
                    else:
                        cs, ct = long_axis(mine, ps), long_axis(theirs, target.bone_position(troles[r]))
                    if np.linalg.norm(cs) > 1e-6 and np.linalg.norm(ct) > 1e-6:
                        rot = swing(r_parent @ cs, ct) @ r_parent
                    if kind == "head" and spec.get("head_from_geometry", True):
                        rs = np.sqrt(((mine - ps) ** 2).sum(1).mean())
                        rt = np.sqrt(((theirs - pt) ** 2).sum(1).mean())
                        size = float(np.clip(rt / max(rs, 1e-9), 0.8 * girth, 2.0 * girth))
                scale = size * np.eye(3)
                if reach is not None and parent is not None and kind in reach_ends and len(mine) and len(theirs):
                    along = unit(pt - tpos[parent])
                    far_s = (((mine - ps) @ (rot @ scale).T) @ along).max()
                    far_t = ((theirs - pt) @ along).max()
                    reach[r] = (far_t - far_s) * along
            rotation[r] = rot
            linear[r] = rot @ scale
        return linear, pivot_s, pivot_t

    # First the plain solve to see where the end parts come out, then again with their joints moved along the limb
    # so that the sole of a foot is where the target's sole is (the two skeletons put the ankle at different heights).
    reach = {}
    solve(tpos, reach)
    moved = {r: tpos[r] + reach.get(r, 0.0) for r in tpos}
    linear, pivot_s, pivot_t = solve(moved, None)
    for r, shift in reach.items():
        if np.linalg.norm(shift) > 1e-4:
            notes.append(f"{r} moved {np.linalg.norm(shift):.3f} m along its limb")

    # Linear blend of the per-role transforms with the source's own weights.
    out = np.zeros_like(positions)
    role_index = {r: i for i, r in enumerate(active)}
    fallback = "hips" if "hips" in role_index else active[0]
    for b, name in enumerate(source.bone_names):
        r = role_of_bone[name] or fallback
        w = source.weights[:, b:b + 1]
        if not w.any():
            continue
        out += w * ((positions - pivot_s[r]) @ linear[r].T + pivot_t[r])

    bones = sorted({troles[r] for r in active})
    column = {b: i for i, b in enumerate(bones)}
    weights = np.zeros((len(positions), len(bones)))
    for b, name in enumerate(source.bone_names):
        r = role_of_bone[name] or fallback
        weights[:, column[troles[r]]] += source.weights[:, b]
    fit = Fit(out, weights, notes)
    fit.bones = bones
    return fit


def fit_blob(source, target, spec):
    notes = []
    positions, _ = source.posed(spec.get("pose"))
    up = axis_vector(spec.get("up", "+y"))
    front = axis_vector(spec.get("front", "+z"))
    front = unit(front - np.dot(front, up) * up)
    rot = np.stack([np.cross(up, front), up, front], axis=1)  # tool x, y, z -> target directions
    local = target.positions @ rot  # the creature in the source's axes
    bones = sorted(set(int(b) for b in target.vertex_bone if b >= 0))
    if not len(target.positions) or len(target.triangles) < 4 or spec.get("size") is not None:
        # A placeholder model (one triangle): the source stands on the first bone at the given height.
        height = float(spec.get("size", 1.7))
        s = height / max(np.ptp(positions[:, 1]), 1e-9)
        centre = np.array([positions[:, 0].mean(), positions[:, 1].min(), positions[:, 2].mean()])
        anchor = np.array(spec.get("origin", [0.0, 0.0, 0.0]), dtype=np.float64)
        out = ((positions - centre) * s) @ rot.T + anchor
        bone = int(spec.get("bone", bones[0] if bones else min(target.bone_world)))
        fit = Fit(out, np.ones((len(positions), 1)), notes + [f"rigid on bone {bone}, height {height}"])
        fit.bones = [bone]
        return fit
    smin, smax = positions.min(0), positions.max(0)
    tmin, tmax = local.min(0), local.max(0)
    ratio = (tmax - tmin) / np.maximum(smax - smin, 1e-9)
    if spec.get("stretch") == "bbox":
        scale = ratio
    else:
        # The body's length decides; the cross-section keeps its shape within a limit, so the figure stays readable.
        cross = float(np.sqrt(ratio[0] * ratio[2]))
        limit = spec.get("cross_limit", [0.6, 1.8])
        cross = float(np.clip(cross, limit[0] * ratio[1], limit[1] * ratio[1]))
        scale = np.array([cross, ratio[1], cross])
    scale = scale * np.array(spec.get("scale", [1.0, 1.0, 1.0]), dtype=np.float64)
    fitted = (positions - (smin + smax) / 2) * scale + (tmin + tmax) / 2 + np.array(spec.get("offset", [0.0, 0.0, 0.0]))
    out = fitted @ rot.T
    notes.append(f"scale {scale.round(4).tolist()}")

    tri = target.triangles
    a, b, c = target.positions[tri[:, 0]], target.positions[tri[:, 1]], target.positions[tri[:, 2]]
    nearest, bary = closest_on_triangles(out, a, b, c)
    column = {bone: i for i, bone in enumerate(bones)}
    weights = np.zeros((len(out), len(bones)))
    for k in range(3):
        cols = np.array([column[int(x)] for x in target.vertex_bone[tri[nearest, k]]])
        np.add.at(weights, (np.arange(len(out)), cols), bary[:, k])
    fit = Fit(out, weights, notes)
    fit.bones = bones
    return fit


def fit_chain(source, target, spec):
    """The source body is bent along a chain of bones (a worm, a tentacle): feet at the first bone, head past the
    last one. Every stretch between two joints follows the bone it hangs on, blended towards its neighbours."""
    notes = []
    positions, _ = source.posed(spec.get("pose"))
    chain = [int(b) for b in spec["chain"]]
    for b in chain:
        if b not in target.bone_world:
            raise ValueError(f"chain: bone {b} is not in the skeleton")
    joints = [target.bone_position(b) for b in chain]

    def reach(bone, origin, direction):
        own = target.part_vertices(bone)
        return float(max(((own - origin) @ direction).max(), 0.0)) if len(own) else 0.0

    # Stretches: a lead-in before the first joint, one per pair of joints, a lead-out past the last; each belongs
    # to the bone whose part it is (of two neighbours, the parent).
    points, owners = [], []
    first_dir = unit(joints[0] - joints[1]) if len(joints) > 1 else np.array([0.0, -1.0, 0.0])
    last_dir = unit(joints[-1] - joints[-2]) if len(joints) > 1 else np.array([0.0, 1.0, 0.0])
    lead_in = reach(chain[0], joints[0], first_dir)
    lead_out = reach(chain[-1], joints[-1], last_dir)
    points.append(joints[0] + first_dir * lead_in)
    if lead_in > 1e-6:
        points.append(joints[0])
        owners.append(chain[0])
    for i in range(len(chain) - 1):
        if np.linalg.norm(joints[i + 1] - joints[i]) < 1e-6:
            continue
        a, b = chain[i], chain[i + 1]
        points.append(joints[i + 1])
        owners.append(b if target.bone_parent.get(a) == b else a)
    if lead_out > 1e-6:
        points.append(joints[-1] + last_dir * lead_out)
        owners.append(chain[-1])
    points = np.array(points)
    seg = points[1:] - points[:-1]
    seg_len = np.linalg.norm(seg, axis=1)
    total = float(seg_len.sum())
    if total < 1e-6:
        raise ValueError("chain: the bones are all in one place")
    start = np.concatenate([[0.0], np.cumsum(seg_len)])
    tangents = seg / seg_len[:, None]

    # A frame per stretch: the face direction is carried along the chain without twisting.
    front = axis_vector(spec.get("front", "+y"))
    normals = []
    n = unit(front - np.dot(front, tangents[0]) * tangents[0])
    if not n.any():
        n = unit(np.cross(tangents[0], [1.0, 0, 0]))
    normals.append(n)
    for i in range(1, len(tangents)):
        n = swing(tangents[i - 1], tangents[i]) @ normals[-1]
        normals.append(unit(n - np.dot(n, tangents[i]) * tangents[i]))
    normals = np.array(normals)
    binormals = np.cross(tangents, normals)

    lo, hi = positions[:, 1].min(), positions[:, 1].max()
    height = max(hi - lo, 1e-9)
    girth = total / height * float(spec.get("girth_scale", 1.0))
    centre_x = (positions[:, 0].min() + positions[:, 0].max()) / 2
    body = positions[np.abs(positions[:, 0] - centre_x) < 0.15 * height]
    centre_z = (body[:, 2].min() + body[:, 2].max()) / 2 if len(body) else 0.0
    s = (positions[:, 1] - lo) / height * total
    index = np.clip(np.searchsorted(start, s, side="right") - 1, 0, len(seg_len) - 1)
    # Near a joint the frame turns gradually from one stretch to the next.
    mid = (start[:-1] + start[1:]) / 2
    left = np.clip(np.searchsorted(mid, s, side="right") - 1, 0, len(mid) - 1)
    right = np.clip(left + 1, 0, len(mid) - 1)
    span = np.maximum(mid[right] - mid[left], 1e-9)
    blend = np.clip((s - mid[left]) / span, 0.0, 1.0)
    blend = np.where(right == left, 0.0, blend)[:, None]

    def mix(table):
        value = (1 - blend) * table[left] + blend * table[right]
        return value / np.maximum(np.linalg.norm(value, axis=1, keepdims=True), 1e-9)

    along = points[index] + tangents[index] * (s - start[index])[:, None]
    out = along + girth * ((positions[:, 0] - centre_x)[:, None] * mix(binormals) +
                           (positions[:, 2] - centre_z)[:, None] * mix(normals))

    bones = sorted(set(owners))
    column = {b: i for i, b in enumerate(bones)}
    weights = np.zeros((len(positions), len(bones)))
    rows = np.arange(len(positions))
    np.add.at(weights, (rows, np.array([column[owners[i]] for i in left])), 1 - blend[:, 0])
    np.add.at(weights, (rows, np.array([column[owners[i]] for i in right])), blend[:, 0])
    notes.append(f"chain of {len(owners)} stretches, {total:.2f} m, scale {girth:.4f}")
    fit = Fit(out, weights, notes)
    fit.bones = bones
    return fit


def smooth_weights(weights, triangles, corners, iterations, strength=0.5):
    """Laplacian smoothing over the mesh edges: softens the hard part borders a rigid-part original leaves."""
    if iterations <= 0:
        return weights
    tri = corners[triangles]
    edges = np.concatenate([tri[:, [0, 1]], tri[:, [1, 2]], tri[:, [2, 0]]])
    edges = np.unique(np.sort(edges, axis=1), axis=0)
    degree = np.zeros(len(weights))
    np.add.at(degree, edges[:, 0], 1)
    np.add.at(degree, edges[:, 1], 1)
    degree = np.maximum(degree, 1)[:, None]
    for _ in range(iterations):
        total = np.zeros_like(weights)
        np.add.at(total, edges[:, 0], weights[edges[:, 1]])
        np.add.at(total, edges[:, 1], weights[edges[:, 0]])
        weights = (1 - strength) * weights + strength * total / degree
    return weights


def limit_influences(weights, count=4):
    order = np.argsort(-weights, axis=1)[:, :count]
    rows = np.arange(len(weights))[:, None]
    top = weights[rows, order]
    top = np.where(top < 1e-4, 0.0, top)
    total = top.sum(1, keepdims=True)
    top = np.where(total > 0, top / np.maximum(total, 1e-12), np.eye(count)[0])
    return order, top


# ---------------------------------------------------------------------------------------------------------------------
# Writing.


def write_glb(target, source, fit, out_path, texture_uri, name):
    doc = target.doc
    old_nodes = doc["nodes"]
    keep = [i for i, n in enumerate(old_nodes) if "mesh" not in n]
    remap = {old: new for new, old in enumerate(keep)}
    nodes = []
    for old in keep:
        n = {k: v for k, v in old_nodes[old].items() if k not in ("children", "skin")}
        children = [remap[c] for c in old_nodes[old].get("children", []) if c in remap]
        if children:
            n["children"] = children
        nodes.append(n)
    scene_roots = [remap[r] for r in doc["scenes"][doc.get("scene", 0)]["nodes"] if r in remap]

    blob = bytearray()
    views, accessors = [], []

    def add(array, component, kind, target_kind=None, bounds=False):
        while len(blob) % 4:
            blob.append(0)
        raw = np.ascontiguousarray(array).tobytes()
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(raw)}
        if target_kind:
            view["target"] = target_kind
        views.append(view)
        blob.extend(raw)
        acc = {"bufferView": len(views) - 1, "componentType": component, "count": len(array), "type": kind}
        if bounds:
            acc["min"] = [float(x) for x in array.min(0)]
            acc["max"] = [float(x) for x in array.max(0)]
        accessors.append(acc)
        return len(accessors) - 1

    skin = doc["skins"][0]
    ibm = accessor(doc, target.binary, skin["inverseBindMatrices"]).astype(np.float32)
    if fit.bind:
        # The model's own bind pose: its inverse bind matrices, and the joints' node transforms to match, so that
        # any glTF viewer shows the model as it is.
        ibm = ibm.copy()
        world = {}
        for j, node_index in enumerate(target.joint_nodes):
            b = target.joint_bone[j]
            if b in fit.bind:
                world[node_index] = fit.bind[b]
                ibm[j] = np.linalg.inv(fit.bind[b]).T.reshape(16).astype(np.float32)
        parent_of = {c: i for i, n in enumerate(old_nodes) for c in n.get("children", [])}
        for node_index, m in world.items():
            p = parent_of.get(node_index)
            local = np.linalg.inv(world[p]) @ m if p in world else m
            n = nodes[remap[node_index]]
            for key in ("translation", "rotation", "scale"):
                n.pop(key, None)
            n["matrix"] = [float(x) for x in local.T.reshape(16)]
    ibm_index = add(ibm, 5126, "MAT4")

    bone_cols, top = limit_influences(fit.bone_weights)
    joint_of_col = np.array([target.bone_joint[b] for b in fit.bones], dtype=np.uint16)
    v_joints = joint_of_col[bone_cols]
    v_joints = np.where(top > 0, v_joints, 0).astype(np.uint16)
    normals = vertex_normals(fit.positions, source.corners[source.triangles])

    pos = fit.positions[source.corners].astype(np.float32)
    nrm = normals[source.corners].astype(np.float32)
    uv = source.uvs.astype(np.float32)
    jnt = v_joints[source.corners]
    wgt = top[source.corners].astype(np.float32)

    primitives, materials, images, textures = [], [], [], []
    for m, mat_name in enumerate(source.material_names):
        tris = source.triangles[source.triangle_material == m]
        if not len(tris):
            continue
        used = np.unique(tris)
        local = np.full(len(pos), -1, dtype=np.int64)
        local[used] = np.arange(len(used))
        index_type, index_dtype = (5123, np.uint16) if len(used) < 65536 else (5125, np.uint32)
        prim = {
            "attributes": {
                "POSITION": add(pos[used], 5126, "VEC3", 34962, bounds=True),
                "NORMAL": add(nrm[used], 5126, "VEC3", 34962),
                "TEXCOORD_0": add(uv[used], 5126, "VEC2", 34962),
                "JOINTS_0": add(jnt[used], 5123, "VEC4", 34962),
                "WEIGHTS_0": add(wgt[used], 5126, "VEC4", 34962),
            },
            "indices": add(local[tris].astype(index_dtype).reshape(-1), index_type, "SCALAR", 34963),
            "material": len(materials),
            "mode": 4,
        }
        primitives.append(prim)
        material = {"name": mat_name, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0,
                                                              "roughnessFactor": 1}}
        uri = texture_uri(mat_name)
        if uri:
            if uri not in images:
                images.append(uri)
                textures.append({"sampler": 0, "source": len(images) - 1})
            material["pbrMetallicRoughness"]["baseColorTexture"] = {"index": images.index(uri)}
        materials.append(material)

    mesh_node = {"name": name, "mesh": 0, "skin": 0}
    nodes.append(mesh_node)
    if scene_roots and "children" in nodes[scene_roots[0]] and nodes[scene_roots[0]].get("name", "").lower()[:4] != "bone":
        nodes[scene_roots[0]]["children"] = [len(nodes) - 1] + nodes[scene_roots[0]]["children"]
    else:
        scene_roots.append(len(nodes) - 1)

    out = {
        "asset": {"version": "2.0", "generator": "silent-hill-pc rig_transfer",
                  "extras": {"skeleton_of": doc.get("asset", {}).get("extras", {}).get("source", ""),
                             **({"proportions": "model"} if fit.bind else {}),
                             "notes": fit.notes}},
        "scene": 0,
        "scenes": [{"name": doc["scenes"][doc.get("scene", 0)].get("name", name), "nodes": scene_roots}],
        "nodes": nodes,
        "meshes": [{"name": name, "primitives": primitives}],
        "skins": [{"name": skin.get("name", name), "inverseBindMatrices": ibm_index,
                   "joints": [remap[j] for j in skin["joints"]]}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    if "skeleton" in skin and skin["skeleton"] in remap:
        out["skins"][0]["skeleton"] = remap[skin["skeleton"]]
    if images:
        out["images"] = [{"uri": u} for u in images]
        out["textures"] = textures
        out["samplers"] = [{"magFilter": 9729, "minFilter": 9729}]
    text = json.dumps(out, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    while len(blob) % 4:
        blob.append(0)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        f.write(struct.pack("<II", len(text), 0x4E4F534A))
        f.write(text)
        f.write(struct.pack("<II", len(blob), 0x004E4942))
        f.write(blob)
    return len(pos), len(source.triangles), int((top > 0).sum(1).max())


def write_manifest(target_path, out_path, source_name):
    """The import manifest next to the glb: it names the disc file of the skeleton, which the engine still reads for
    the part list (characters)."""
    stem = os.path.splitext(target_path)[0]
    disc = {}
    try:
        with open(stem + ".model.json", "r", encoding="utf-8") as f:
            disc = json.load(f).get("source", {})
    except OSError:
        pass
    if not disc.get("path"):
        return False
    manifest = {
        "format": "sh-model/1",
        "description": f"{source_name} bound to the skeleton of {disc['path']} by tools/modding/rig_transfer.",
        "source": {"path": disc["path"], "kind": disc.get("kind", "ilm")},
    }
    with open(os.path.splitext(out_path)[0] + ".model.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    return True


# ---------------------------------------------------------------------------------------------------------------------


def inspect(args):
    """Prints the skeleton of the named targets: what a roles table is written from."""
    for name in args.inspect:
        target = Target(os.path.join(args.models, name if name.lower().endswith(".glb") else name + ".glb"))
        lo, hi = target.positions.min(0).round(3).tolist(), target.positions.max(0).round(3).tolist()
        print(f"{name}: {len(target.bone_world)} bones, {len(target.positions)} vertices, "
              f"{len(target.triangles)} triangles, box {lo} .. {hi} (metres; +Y up, the character faces +Z)")
        parts = {}
        for node in target.doc["nodes"]:
            if "mesh" in node:
                parts.setdefault(int(node.get("extras", {}).get("bone", -1)), []).append(node.get("name", ""))
        for bone in sorted(target.bone_world):
            x, y, z = target.bone_position(bone)
            own = target.part_vertices(bone)
            centre = "  -      -      -   " if not len(own) else "%6.2f %6.2f %6.2f" % tuple(own.mean(0))
            print(f"  bone{bone:02d} parent {target.bone_parent[bone]:3d}  at {x:7.3f} {y:7.3f} {z:7.3f}  "
                  f"vertices {len(own):4d} around {centre}  parts {' '.join(parts.get(bone, []))}")
    return 0


def run(args):
    if args.inspect:
        return inspect(args)
    if not (args.source and args.targets and args.out):
        print("--source, --targets and --out are needed (or --inspect NAME ...)")
        return 2
    source = Source(args.source, args.source_file)
    with open(args.targets, "r", encoding="utf-8") as f:
        targets = json.load(f)["targets"]
    source_name = source.profile.get("name", os.path.splitext(os.path.basename(args.source))[0])
    material_images = source.profile.get("textures", {})
    prefix = args.texture_uri

    def texture_uri(material):
        image = material_images.get(material)
        return (prefix + image) if image else None

    print(f"source {source_name}: {len(source.positions)} vertices, {len(source.triangles)} triangles, "
          f"{len(source.bone_names)} bones, materials {source.material_names}")
    failed = 0
    names = args.only or sorted(targets)
    for name in names:
        spec = targets.get(name)
        if spec is None:
            print(f"{name}: not in {args.targets}")
            failed += 1
            continue
        path = os.path.join(args.models, spec.get("glb", name + ".glb"))
        try:
            target = Target(path)
            mode = spec.get("mode", "rig")
            fitters = {"rig": fit_rig, "blob": fit_blob, "chain": fit_chain}
            if mode not in fitters:
                raise ValueError(f"unknown mode {mode!r} (rig, blob, chain)")
            fit = fitters[mode](source, target, spec)
            smooth = int(spec.get("smooth", 3 if mode == "blob" else 0))
            fit.bone_weights = smooth_weights(fit.bone_weights, source.triangles, source.corners, smooth)
            if not np.isfinite(fit.positions).all():
                raise ValueError("the fit produced non-finite positions")
            out_path = os.path.join(args.out, name + ".glb")
            vertices, triangles, influences = write_glb(target, source, fit, out_path, texture_uri, source_name)
            manifest = write_manifest(path, out_path, source_name)
            size = np.ptp(fit.positions, axis=0).round(2).tolist()
            print(f"{name}: {mode}, {len(target.bone_world)} bones ({len(fit.bones)} used), {vertices} vertices, "
                  f"{triangles} triangles, <= {influences} influences, size {size} m, "
                  f"{'; '.join(fit.notes)}{'' if manifest else '; NO manifest (no .model.json next to the target)'}")
        except (OSError, ValueError, KeyError) as error:
            print(f"{name}: FAILED: {error}")
            failed += 1
    return 1 if failed else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--source", help="source profile (.json): the model file, its axes, bone roles, textures")
    p.add_argument("--source-file", help="the model file, when it is not where the profile's \"file\" says")
    p.add_argument("--targets", help="targets file (.json): per character glb the fit mode and bone roles")
    p.add_argument("--models", required=True, help="folder of the imported character glbs (<gamedata>/convert/models/CHARA)")
    p.add_argument("--out", help="output folder (<mod>/convert/models/<folder>)")
    p.add_argument("--inspect", nargs="+", metavar="NAME", help="print the bones and parts of these character glbs and stop")
    p.add_argument("--texture-uri", default="", help="prefix of the image URIs written into the glbs, relative to --out "
                   "(\"../../textures/<folder>/\")")
    p.add_argument("--only", nargs="*", help="target names to build (default: all of the targets file)")
    sys.exit(run(p.parse_args()))


if __name__ == "__main__":
    main()
