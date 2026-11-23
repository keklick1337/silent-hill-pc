# SPDX-License-Identifier: GPL-3.0-or-later
"""Reader of ASCII FBX 6.x files (3ds Max / old FBX SDK exports) for rig_transfer.

Blender imports only binary FBX 7.x, and many old game-model rips are ASCII 6.1. This reader takes what a rig
transfer needs: the skinned mesh (positions, polygons, per-corner normals and UVs, per-polygon materials), the skin
clusters (bone, vertex indices, weights, the bone's world matrix at bind time) and the bone hierarchy.

Everything is returned in the file's world space at bind time (the mesh node's bind matrix applied).
"""

import re

import numpy as np

_TOKEN = re.compile(r'"[^"]*"|[A-Za-z_][A-Za-z0-9_]*:|[{},]|[^\s,{}]+')


class Node:
    __slots__ = ("name", "values", "children")

    def __init__(self, name):
        self.name = name
        self.values = []
        self.children = []

    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def all(self, name):
        return [c for c in self.children if c.name == name]


def _value(tok):
    if tok.startswith('"'):
        return tok[1:-1]
    try:
        return int(tok)
    except ValueError:
        try:
            return float(tok)
        except ValueError:
            return tok


def parse(text):
    """The node tree of an ASCII FBX file."""
    tokens = []
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith(";"):
            continue
        tokens.extend(_TOKEN.findall(line))
    root = Node("")
    stack = [root]
    current = None
    for tok in tokens:
        if tok == "{":
            stack.append(current)
        elif tok == "}":
            stack.pop()
            current = None
        elif tok == ",":
            continue
        elif tok.endswith(":") and not tok.startswith('"'):
            current = Node(tok[:-1])
            stack[-1].children.append(current)
        elif current is not None:
            current.values.append(_value(tok))
    return root


def _matrix(values):
    # FBX writes matrices for row vectors (translation in the last row); the tool multiplies column vectors.
    return np.array(values, dtype=np.float64).reshape(4, 4).T


def _short(name):
    return name.split("::", 1)[1] if "::" in name else name


def load(path):
    """Returns a dict: positions (n,3), polygons (list of index lists), corner_normals, corner_uvs, polygon_material,
    materials (names), bones {name: {"parent", "world" (4x4 at bind)}}, weights {bone: (indices, weights)}."""
    with open(path, "r", encoding="latin-1") as f:
        root = parse(f.read())
    header = root.find("FBXHeaderExtension")
    version = header.find("FBXVersion").values[0] if header and header.find("FBXVersion") else 0
    if not 6000 <= version < 7000:
        raise ValueError(f"{path}: FBX version {version}: only ASCII FBX 6.x is read here")
    objects = root.find("Objects")
    connections = root.find("Connections")

    parent = {}
    material_order = {}
    cluster_bone = {}
    for c in connections.all("Connect"):
        kind, child, par = c.values[0], c.values[1], c.values[2]
        if kind != "OO":
            continue
        if child.startswith("Model::") and par.startswith("Model::"):
            parent[_short(child)] = _short(par)
        elif child.startswith("Material::") and par.startswith("Model::"):
            material_order.setdefault(_short(par), []).append(_short(child))
        elif child.startswith("Model::") and par.startswith("SubDeformer::"):
            cluster_bone[par] = _short(child)

    mesh_matrix = {}
    for pose in objects.all("Pose"):
        for pn in pose.all("PoseNode"):
            mesh_matrix[_short(pn.find("Node").values[0])] = _matrix(pn.find("Matrix").values)

    meshes = [m for m in objects.all("Model") if m.values[1] == "Mesh" and m.find("Vertices")]
    if not meshes:
        raise ValueError(f"{path}: no mesh")
    mesh = max(meshes, key=lambda m: len(m.find("Vertices").values))
    mesh_name = _short(mesh.values[0])
    to_world = mesh_matrix.get(mesh_name, np.eye(4))

    local = np.array(mesh.find("Vertices").values, dtype=np.float64).reshape(-1, 3)
    positions = local @ to_world[:3, :3].T + to_world[:3, 3]
    polygons, poly = [], []
    for i in mesh.find("PolygonVertexIndex").values:
        if i < 0:
            poly.append(-i - 1)
            polygons.append(poly)
            poly = []
        else:
            poly.append(i)
    corners = sum(len(p) for p in polygons)

    corner_normals = None
    ln = mesh.find("LayerElementNormal")
    if ln is not None:
        normals = np.array(ln.find("Normals").values, dtype=np.float64).reshape(-1, 3) @ to_world[:3, :3].T
        mapping = ln.find("MappingInformationType").values[0]
        if mapping == "ByPolygonVertex" and len(normals) == corners:
            corner_normals = normals
        elif mapping in ("ByVertice", "ByVertex") and len(normals) == len(positions):
            corner_normals = normals[[i for p in polygons for i in p]]

    corner_uvs = None
    lu = mesh.find("LayerElementUV")
    if lu is not None:
        uv = np.array(lu.find("UV").values, dtype=np.float64).reshape(-1, 2)
        index = lu.find("UVIndex")
        mapping = lu.find("MappingInformationType").values[0]
        if mapping == "ByPolygonVertex":
            corner_uvs = uv[index.values] if index is not None and index.values else uv
        elif mapping in ("ByVertice", "ByVertex"):
            flat = [i for p in polygons for i in p]
            corner_uvs = uv[[index.values[i] for i in flat]] if index is not None and index.values else uv[flat]

    polygon_material = np.zeros(len(polygons), dtype=np.int64)
    lm = mesh.find("LayerElementMaterial")
    if lm is not None and lm.find("Materials") is not None:
        values = lm.find("Materials").values
        if lm.find("MappingInformationType").values[0] == "ByPolygon" and len(values) == len(polygons):
            polygon_material = np.array(values, dtype=np.int64)
        elif values:
            polygon_material[:] = values[0]

    bones, weights = {}, {}
    for model in objects.all("Model"):
        if model.values[1] in ("LimbNode", "Limb", "Root", "Null"):
            name = _short(model.values[0])
            bones[name] = {"parent": parent.get(name), "world": None}
    for d in objects.all("Deformer"):
        if d.values[1] != "Cluster":
            continue
        name = cluster_bone.get(d.values[0]) or d.values[0].split("Cluster ", 1)[-1]
        bones.setdefault(name, {"parent": parent.get(name), "world": None})
        link = d.find("TransformLink")
        if link is not None:
            bones[name]["world"] = _matrix(link.values)
        idx, w = d.find("Indexes"), d.find("Weights")
        if idx is not None and w is not None and idx.values:
            weights[name] = (np.array(idx.values, dtype=np.int64), np.array(w.values, dtype=np.float64))
    for b in bones.values():
        if b["parent"] not in bones:
            b["parent"] = None

    return {
        "name": mesh_name,
        "positions": positions,
        "polygons": polygons,
        "corner_normals": corner_normals,
        "corner_uvs": corner_uvs,
        "polygon_material": polygon_material,
        "materials": material_order.get(mesh_name, []),
        "bones": bones,
        "weights": weights,
    }


if __name__ == "__main__":
    import sys

    m = load(sys.argv[1])
    print(m["name"], len(m["positions"]), "vertices,", len(m["polygons"]), "polygons, materials", m["materials"])
    print("bbox", m["positions"].min(0).round(3), m["positions"].max(0).round(3))
    for name, b in m["bones"].items():
        w = m["weights"].get(name)
        pos = None if b["world"] is None else b["world"][:3, 3].round(2).tolist()
        print(f"  {name!r:32} parent {b['parent']!r:28} at {pos} verts {0 if w is None else len(w[0])}")
