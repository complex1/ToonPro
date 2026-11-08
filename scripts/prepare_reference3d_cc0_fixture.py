#!/usr/bin/env python3
"""Prepare an optional MPFB CC0 rigged import fixture, without running MPFB.

Reads graphical asset data only. No MakeHuman/MPFB program code is copied into
Toon Pro. The resulting GLB is a local review fixture, not a bundled starter.
Obtain makehumancommunity/mpfb2 at the pinned revision, then pass its directory.
"""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

REVISION = "6ff1eb358f6c1959ced77e79e6dd2406d9a71f49"
SOURCE_URL = "https://github.com/makehumancommunity/mpfb2"


def prepare(source: Path, output: Path) -> None:
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        raise ValueError(f"Expected MPFB revision {REVISION}, received {revision}")
    data = source / "src/mpfb/data"
    used = {}

    def read(path: Path) -> bytes:
        value = path.read_bytes()
        used[str(path.relative_to(source))] = hashlib.sha256(value).hexdigest()
        return value

    license_text = read(source / "LICENSE.ASSETS.md")
    read(source / "LICENSE.md")
    weights_data = json.loads(read(data / "rigs/standard/weights.mixamo.json"))
    if weights_data.get("license") != "CC0":
        raise ValueError("The selected rig weights must explicitly declare CC0")
    rig = json.loads(read(data / "rigs/standard/rig.mixamo.json"))["bones"]
    vertices, faces, groups = [], [], {}
    group = ""
    for line in read(data / "3dobjs/base.obj").decode().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(tuple(float(x) for x in fields[1:4]))
        elif fields[0] == "g":
            group = fields[1]
        elif fields[0] == "f":
            polygon = [int(x.split("/")[0]) - 1 for x in fields[1:]]
            groups.setdefault(group, set()).update(polygon)
            if group == "body":
                faces.extend((polygon[0], polygon[i], polygon[i + 1]) for i in range(1, len(polygon) - 1))
    # The body group is separate from joint cubes, clothing, eyes and other helpers.
    selected = sorted(groups["body"])
    remap = {value: index for index, value in enumerate(selected)}
    ymin = min(vertices[i][1] for i in selected)
    ymax = max(vertices[i][1] for i in selected)
    scale = 1.75 / (ymax - ymin)

    def world(point):
        return [point[0] * scale, (point[1] - ymin) * scale, point[2] * scale]

    positions = [world(vertices[i]) for i in selected]
    bone_names = []

    def visit(name):
        if name in bone_names:
            return
        if rig[name]["parent"]:
            visit(rig[name]["parent"])
        bone_names.append(name)

    for name in sorted(rig):
        visit(name)
    bone_index = {name: i for i, name in enumerate(bone_names)}
    points = {}
    for name in bone_names:
        definition = rig[name]["head"]
        indices = definition["vertex_indices"] if definition["strategy"] == "MEAN" else sorted(groups[definition["cube_name"]])
        points[name] = world([sum(vertices[i][a] for i in indices) / len(indices) for a in range(3)])
    nodes = []
    for name in bone_names:
        parent = rig[name]["parent"]
        origin = points[parent] if parent else [0, 0, 0]
        node = {"name": name, "translation": [points[name][a] - origin[a] for a in range(3)]}
        children = [bone_index[n] for n in bone_names if rig[n]["parent"] == name]
        if children:
            node["children"] = children
        nodes.append(node)
    vertex_weights = {i: [] for i in selected}
    for name, values in weights_data["weights"].items():
        for index, amount in values:
            if index in vertex_weights and amount > 0:
                vertex_weights[index].append((bone_index[name], amount))
    joints, weights, discarded = [], [], []
    for i in selected:
        ordered = sorted(vertex_weights[i], key=lambda item: (-item[1], item[0]))
        if not ordered:
            raise ValueError(f"Unweighted body vertex {i}")
        discarded.append(sum(x[1] for x in ordered[4:]))
        kept = ordered[:4]
        total = sum(x[1] for x in kept)
        joints.append([x[0] for x in kept] + [0] * (4 - len(kept)))
        weights.append([x[1] / total for x in kept] + [0] * (4 - len(kept)))
    normals = [[0.0, 0.0, 0.0] for _ in selected]
    indices = []
    for face in faces:
        a, b, c = (remap[i] for i in face)
        indices.extend((a, b, c))
        ab = [positions[b][i] - positions[a][i] for i in range(3)]
        ac = [positions[c][i] - positions[a][i] for i in range(3)]
        normal = [ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]]
        for index in (a, b, c):
            for axis in range(3):
                normals[index][axis] += normal[axis]
    normals = [[x / max(1e-12, math.sqrt(sum(v * v for v in n))) for x in n] for n in normals]
    binary = bytearray()
    views, accessors = [], []

    def accessor(values, fmt, component, kind):
        while len(binary) % 4:
            binary.append(0)
        start = len(binary)
        for value in values:
            binary.extend(struct.pack("<" + fmt, *value))
        views.append({"buffer": 0, "byteOffset": start, "byteLength": len(binary) - start})
        accessors.append({"bufferView": len(views) - 1, "componentType": component, "count": len(values), "type": kind})
        if kind == "VEC3":
            accessors[-1]["min"] = [min(v[a] for v in values) for a in range(3)]
            accessors[-1]["max"] = [max(v[a] for v in values) for a in range(3)]
        return len(accessors) - 1

    primitive = {"attributes": {"POSITION": accessor(positions, "3f", 5126, "VEC3"), "NORMAL": accessor(normals, "3f", 5126, "VEC3"),
        "JOINTS_0": accessor(joints, "4H", 5123, "VEC4"), "WEIGHTS_0": accessor(weights, "4f", 5126, "VEC4")},
        "indices": accessor([[x] for x in indices], "I", 5125, "SCALAR"), "material": 0}
    ibm = []
    for name in bone_names:
        x, y, z = points[name]
        ibm.append([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1])
    bind_accessor = accessor(ibm, "16f", 5126, "MAT4")
    # A small set of actual CC0 facial targets exercises imported morph editing.
    target_names = []
    for relative in ("chin/chin-width-incr", "chin/chin-height-incr", "chin/chin-prominent-incr", "head/head-scale-horiz-incr", "nose/nose-width1-incr"):
        path = data / "targets" / (relative + ".target.gz")
        if not path.exists():
            continue
        deltas = [[0.0, 0.0, 0.0] for _ in selected]
        for line in gzip.decompress(read(path)).decode().splitlines():
            fields = line.split()
            if not fields or fields[0].startswith("#"):
                continue
            index = int(fields[0])
            if index in remap:
                deltas[remap[index]] = [float(x) * scale for x in fields[1:4]]
        primitive.setdefault("targets", []).append({"POSITION": accessor(deltas, "3f", 5126, "VEC3")})
        target_names.append(relative.split("/")[-1])
    semantic = {"Hips": "hips", "Spine": "spine", "Spine1": "chest", "Spine2": "upperChest", "Neck": "neck", "Head": "head"}
    for side in ("Left", "Right"):
        for suffix, target in {"Shoulder": "Shoulder", "Arm": "UpperArm", "ForeArm": "LowerArm", "Hand": "Hand", "UpLeg": "UpperLeg", "Leg": "LowerLeg", "Foot": "Foot", "ToeBase": "Toes"}.items():
            semantic[side + suffix] = side.lower() + target
        for finger in ("Thumb", "Index", "Middle", "Ring", "Pinky"):
            for i, segment in enumerate(("Metacarpal", "Proximal", "Distal") if finger == "Thumb" else ("Proximal", "Intermediate", "Distal"), 1):
                semantic[side + "Hand" + finger + str(i)] = side.lower() + ("Little" if finger == "Pinky" else finger) + segment
    mappings = {semantic[n.split(":")[-1]]: {"node": i} for i, n in enumerate(bone_names) if n.split(":")[-1] in semantic}
    nodes.append({"name": "CC0 anatomy surface", "mesh": 0, "skin": 0})
    root = {"asset": {"version": "2.0", "generator": "Toon Pro CC0 review fixture converter", "copyright": "MPFB core graphical assets: CC0 1.0 Universal"},
        "scene": 0, "scenes": [{"name": "CC0 anatomy reference", "nodes": [i for i, n in enumerate(bone_names) if not rig[n]["parent"]] + [len(nodes) - 1]}],
        "nodes": nodes, "skins": [{"joints": list(range(len(bone_names))), "inverseBindMatrices": bind_accessor}],
        "meshes": [{"name": "CC0 anatomy reference", "primitives": [primitive], "weights": [0] * len(target_names), "extras": {"targetNames": target_names}}],
        "materials": [{"name": "Neutral clay", "pbrMetallicRoughness": {"baseColorFactor": [.68, .65, .60, 1], "metallicFactor": 0, "roughnessFactor": .85}}],
        "bufferViews": views, "accessors": accessors, "buffers": [{"byteLength": len(binary)}], "extensionsUsed": ["VRMC_vrm"],
        "extensions": {"VRMC_vrm": {"specVersion": "1.0", "meta": {"name": "CC0 anatomy reference", "version": "1", "authors": ["MakeHuman Community"],
            "licenseUrl": "https://creativecommons.org/publicdomain/zero/1.0/", "allowRedistribution": True}, "humanoid": {"humanBones": mappings}}}}
    js = json.dumps(root, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)) + struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(binary), 0x004E4942) + binary
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(glb)
    output.with_suffix(".CC0.txt").write_bytes(license_text)
    report = {"source": SOURCE_URL, "revision": revision, "assetLicense": "CC0-1.0", "inputSha256": used,
        "outputSha256": hashlib.sha256(glb).hexdigest(), "vertices": len(selected), "triangles": len(faces), "bones": len(bone_names),
        "morphs": target_names, "heightMeters": 1.75, "discardedInfluenceVertices": sum(x > 0 for x in discarded), "maxDiscardedWeight": max(discarded),
        "notes": "Optional local import fixture. Helpers removed; normals regenerated; original CC0 weights reduced to four during offline authoring, renormalized and reported. Not a bundled Toon Pro starter or a complete VRM conformance claim."}
    output.with_suffix(".provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({key: report[key] for key in ("vertices", "triangles", "bones", "morphs", "discardedInfluenceVertices", "maxDiscardedWeight")}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source_root.resolve(), args.output.resolve())
