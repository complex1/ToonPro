#!/usr/bin/env python3
"""Reproduce the lightweight dog, cat and eagle reference assets offline.

Download the pinned source archive, export with Blender 5.2, sample the authored
Idle/Walking/Flying clips, remove runtime animation channels, then optionally
package validated canonical assets using toon_reference3d_prepare_pets.
The default output is a separate build directory, never the bundled resources.
"""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import urllib.request
import zipfile

SOURCE_URL = "https://opengameart.org/sites/default/files/Animal%20Pack%20Vol.2%20by%20%40Quaternius.zip"
SOURCE_PAGE = "https://opengameart.org/content/animated-animales-low-poly"
SOURCE_SHA256 = "6d8f6789d65dbdb352ebad98a4209f26b41b384e87d81bc05e9ca76139db1248"
KINDS = ("dog", "cat", "bird")


def normalized(q):
    length = math.sqrt(sum(v*v for v in q))
    return [v/length for v in q]


def slerp(a, b, t):
    a, b = normalized(a), normalized(b)
    dot = sum(x*y for x, y in zip(a, b))
    if dot < 0:
        b, dot = [-v for v in b], -dot
    if dot > .9995:
        return normalized([x+(y-x)*t for x, y in zip(a, b)])
    angle = math.acos(max(-1, min(1, dot)))
    sine = math.sin(angle)
    return [(x*math.sin((1-t)*angle)+y*math.sin(t*angle))/sine for x, y in zip(a, b)]


def read_glb(path):
    raw = path.read_bytes()
    if struct.unpack_from("<III", raw) != (0x46546C67, 2, len(raw)):
        raise ValueError("Invalid exported GLB header: " + str(path))
    length, kind = struct.unpack_from("<II", raw, 12)
    if kind != 0x4E4F534A:
        raise ValueError("Missing GLB JSON chunk")
    offset = 20+length
    binary_length, kind = struct.unpack_from("<II", raw, offset)
    if kind != 0x004E4942 or offset+8+binary_length != len(raw):
        raise ValueError("Unexpected GLB binary chunk layout")
    return json.loads(raw[20:20+length]), raw[offset+8:]


def write_glb(path, document, binary):
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    body = struct.pack("<II", len(encoded), 0x4E4F534A)+encoded
    body += struct.pack("<II", len(binary), 0x004E4942)+binary
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, len(body)+12)+body)


def accessor(document, data, index):
    a = document["accessors"][index]
    view = document["bufferViews"][a["bufferView"]]
    components = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    fmt = {5126: "f", 5125: "I", 5123: "H", 5121: "B"}[a["componentType"]]
    size = struct.calcsize("<"+fmt)*components
    start = view.get("byteOffset", 0)+a.get("byteOffset", 0)
    return [list(struct.unpack_from("<"+str(components)+fmt, data, start+i*view.get("byteStride", size))) for i in range(a["count"])]


def snapshot(document, data, animation, fraction):
    parents = {child: i for i, node in enumerate(document["nodes"]) for child in node.get("children", [])}
    nodes = [dict(name=n["name"], parent=parents.get(i, -1),
                  translation=n.get("translation", [0, 0, 0]), rotation=n.get("rotation", [0, 0, 0, 1]),
                  scale=n.get("scale", [1, 1, 1])) for i, n in enumerate(document["nodes"])]
    clip = next(a for a in document["animations"] if a["name"] == animation)
    time = max(accessor(document, data, s["input"])[-1][0] for s in clip["samplers"])*fraction
    for channel in clip["channels"]:
        sampler = clip["samplers"][channel["sampler"]]
        times = [v[0] for v in accessor(document, data, sampler["input"])]
        values = accessor(document, data, sampler["output"])
        upper = min(len(times)-1, bisect.bisect_left(times, time))
        lower = max(0, upper-1)
        t = (time-times[lower])/(times[upper]-times[lower]) if upper != lower else 0
        mode, path = sampler.get("interpolation", "LINEAR"), channel["target"]["path"]
        if mode == "STEP":
            value = values[lower]
        elif mode == "LINEAR":
            value = slerp(values[lower], values[upper], t) if path == "rotation" else [a+(b-a)*t for a, b in zip(values[lower], values[upper])]
        else:
            raise ValueError("Unprepared interpolation " + mode)
        nodes[channel["target"]["node"]][path] = value
    return nodes


def choices(kind):
    if kind == "bird":
        return [("flying-up", "Wings raised", "Flying", .25), ("flying-spread", "Wings spread", "Flying", 0),
                ("flying-down", "Downstroke", "Flying", .75), ("wings-lowered", "Wings lowered", "Idle", 0),
                ("wings-lowered-study", "Lowered wings study", "Idle", .5)]
    return [("standing", "Standing", "Idle", 0), ("idle-alert", "Idle study", "Idle", .5),
            ("walking-left", "Walking contact A", "Walking", .125), ("walking-right", "Walking contact B", "Walking", .625)]


def extract(work):
    rigs, inventory = [], []
    for kind in KINDS:
        path = work/"prepared"/(kind+".glb")
        document, binary = read_glb(path)
        authored = path.with_name(kind+"-authored.glb")
        authored.write_bytes(path.read_bytes())
        poses = [dict(id="pet/"+kind+"/"+id, name=name, sourceClip=clip, sourceFraction=fraction,
                      nodes=snapshot(document, binary, clip, fraction)) for id, name, clip, fraction in choices(kind)]
        rigs.append(dict(kind=kind, nodes=poses[0]["nodes"], poses=poses))
        primitives = [p for m in document["meshes"] for p in m["primitives"]]
        inventory.append(dict(kind=kind, authoredSha256=hashlib.sha256(authored.read_bytes()).hexdigest(),
                              nodes=len(document["nodes"]), joints=len(document["skins"][0]["joints"]),
                              vertices=sum(document["accessors"][p["attributes"]["POSITION"]]["count"] for p in primitives),
                              triangles=sum(document["accessors"][p["indices"]]["count"]//3 for p in primitives),
                              clips=[a["name"] for a in document["animations"]], staticPoses=[p["id"] for p in poses]))
        del document["animations"]  # Snapshots retained above; runtime importer rejects clip playback.
        write_glb(path, document, binary)
        inventory[-1]["staticGlbSha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    (work/"pet-poses-source.json").write_text(json.dumps(dict(version=1, rigs=rigs), separators=(",", ":"))+"\n")
    (work/"prepared-inventory.json").write_text(json.dumps(inventory, indent=2)+"\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("build/reference3d-pets-reproduced"))
    parser.add_argument("--archive", type=Path, help="Use an already downloaded source ZIP; checksum is still verified")
    parser.add_argument("--blender", type=Path, default=Path("/Applications/Blender.app/Contents/MacOS/Blender"))
    parser.add_argument("--canonical-tool", type=Path, help="Path to the opt-in toon_reference3d_prepare_pets executable")
    parser.add_argument("--resource-dir", type=Path, help="Canonical destination; defaults to OUTPUT/canonical")
    args = parser.parse_args()
    if args.resource_dir and not args.canonical_tool:
        parser.error("--resource-dir requires --canonical-tool")
    if args.canonical_tool and not args.canonical_tool.is_file():
        parser.error("The canonical tool does not exist: " + str(args.canonical_tool))
    if not args.blender.is_file():
        parser.error("The Blender executable does not exist: " + str(args.blender))
    work = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    archive = args.archive.resolve() if args.archive else work/"quaternius-animal-pack-vol2.zip"
    if not archive.exists():
        request = urllib.request.Request(SOURCE_URL, headers={"User-Agent": "ToonPro-reference-review"})
        with urllib.request.urlopen(request, timeout=60) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SOURCE_SHA256:
        raise ValueError("Pinned animal archive checksum differs")
    source = work/"source-animals"
    source.mkdir(exist_ok=True)
    with zipfile.ZipFile(archive) as pack:
        for member in pack.infolist():
            if not (source/member.filename).resolve().is_relative_to(source.resolve()):
                raise ValueError("Unexpected archive path")
        pack.extractall(source)
    exporter = Path(__file__).with_name("reference3d_pets_blender.py").resolve()
    subprocess.run([str(args.blender), "--background", "--python", str(exporter), "--", str(work)], check=True)
    extract(work)
    if args.canonical_tool:
        resources = (args.resource_dir or work/"canonical").resolve()
        subprocess.run([str(args.canonical_tool.resolve()), str(work), str(resources)], check=True)
    print("Prepared static pet inputs in", work)


if __name__ == "__main__":
    main()
