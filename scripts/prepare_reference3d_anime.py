#!/usr/bin/env python3
"""Fetch pinned official VRM examples and prepare local reference review inputs.

Only explicit identity KHR_texture_transform declarations are removed. Seed-san
textures are resampled to at most 768px for the native decoded-texture budget.
Vertex data, material bindings, skins, bind matrices, morphs and avatar metadata
are retained. Nonidentity transforms fail rather than changing UV behavior.
Native import/evaluation and canonical packaging are a separate checked step.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import urllib.request

SOURCES = [
    dict(id="anime/seed-san", name="Seed-san", file="seed-san.vrm",
         url="https://raw.githubusercontent.com/vrm-c/vrm-specification/94e82dd346fa6cf0337c4421728640e5252dd38e/samples/Seed-san/vrm/Seed-san.vrm",
         sha256="624d0d554bc205bbdc33e22a68a2c3c20edebb3e573011ead8878a65e5329b23", textureLimit=768,
         excludedMeshNames=["robo_arm"]),
    dict(id="anime/alicia-solid", name="Alicia Solid", file="alicia-solid.vrm",
         url="https://raw.githubusercontent.com/vrm-c/UniVRM/efd20f62f1bf0ac3fcc34de1e497324c5197d146/Tests/Models/Alicia_vrm-0.51/AliciaSolid_vrm-0.51.vrm",
         sha256="237bb02efadf8c13a114af91dd8e860173081457dee87017e51011c448d05dc2", yaw180=True),
]


def remove_identity_transforms(value):
    count = 0
    if isinstance(value, list):
        return sum(remove_identity_transforms(item) for item in value)
    if not isinstance(value, dict):
        return 0
    extensions = value.get("extensions", {})
    transform = extensions.get("KHR_texture_transform")
    if transform is not None:
        if (set(transform) - {"offset", "scale", "rotation", "texCoord"}
                or transform.get("offset", [0, 0]) != [0, 0]
                or transform.get("scale", [1, 1]) != [1, 1]
                or transform.get("rotation", 0) != 0
                or transform.get("texCoord", value.get("texCoord", 0)) != value.get("texCoord", 0)):
            raise ValueError("Nonidentity texture transform needs an explicit UV bake")
        del extensions["KHR_texture_transform"]
        if not extensions:
            del value["extensions"]
        count += 1
    return count + sum(remove_identity_transforms(item) for item in value.values())


def prepare(data, texture_limit=0):
    if len(data) < 20 or struct.unpack_from("<III", data) != (0x46546C67, 2, len(data)):
        raise ValueError("Expected a complete GLB2/VRM input")
    chunks, offset = [], 12
    while offset < len(data):
        size, kind = struct.unpack_from("<II", data, offset)
        end = offset + 8 + size
        if end > len(data):
            raise ValueError("Invalid GLB chunk length")
        chunks.append((kind, data[offset + 8:end]))
        offset = end
    if not chunks or chunks[0][0] != 0x4E4F534A:
        raise ValueError("The first GLB chunk must contain JSON")
    document = json.loads(chunks[0][1])
    count = remove_identity_transforms(document)
    resized = []
    if texture_limit:
        resizer = shutil.which("sips")
        if not resizer:
            raise ValueError("This preparation step requires macOS sips for bounded PNG resampling")
        if len(chunks) != 2 or chunks[1][0] != 0x004E4942 or len(document["buffers"]) != 1:
            raise ValueError("Texture preparation expects one embedded GLB binary buffer")
        binary = bytearray(chunks[1][1])
        with tempfile.TemporaryDirectory(prefix="toon-anime-images-") as temporary:
            temporary = Path(temporary)
            for index, image in enumerate(document.get("images", [])):
                view = document["bufferViews"][image["bufferView"]]
                start = view.get("byteOffset", 0)
                payload = binary[start:start + view["byteLength"]]
                if payload[:8] != b"\x89PNG\r\n\x1a\n":
                    raise ValueError("Pinned texture preparation expects PNG image payloads")
                width, height = struct.unpack_from(">II", payload, 16)
                if max(width, height) <= texture_limit:
                    continue
                original, reduced = temporary / "original.png", temporary / "reduced.png"
                original.write_bytes(payload)
                subprocess.run([resizer, "-Z", str(texture_limit), str(original), "--out", str(reduced)],
                               check=True, stdout=subprocess.DEVNULL)
                encoded_image = reduced.read_bytes()
                new_width, new_height = struct.unpack_from(">II", encoded_image, 16)
                binary.extend(b"\0" * (-len(binary) % 4))
                image["bufferView"] = len(document["bufferViews"])
                document["bufferViews"].append(dict(buffer=0, byteOffset=len(binary), byteLength=len(encoded_image)))
                binary.extend(encoded_image)
                resized.append(dict(image=index, oldSize=[width, height], newSize=[new_width, new_height]))
        binary.extend(b"\0" * (-len(binary) % 4))
        document["buffers"][0]["byteLength"] = len(binary)
        chunks[1] = (0x004E4942, bytes(binary))
    if not count and not resized:
        return data, 0, []
    for key in ("extensionsUsed", "extensionsRequired"):
        if key in document:
            document[key] = [name for name in document[key] if name != "KHR_texture_transform"]
    encoded = json.dumps(document, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    chunks[0] = (0x4E4F534A, encoded)
    body = b"".join(struct.pack("<II", len(payload), kind) + payload for kind, payload in chunks)
    return struct.pack("<III", 0x46546C67, 2, len(body) + 12) + body, count, resized


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("build/reference3d-anime-pets"))
    parser.add_argument("--canonical-tool", type=Path, help="Optional built toon_reference3d_prepare_asset executable")
    parser.add_argument("--resource-dir", type=Path, default=Path("resources/reference3d"))
    args = parser.parse_args()
    source, ready = args.output_dir / "anime-source", args.output_dir / "readyassets"
    source.mkdir(parents=True, exist_ok=True)
    ready.mkdir(parents=True, exist_ok=True)
    manifest = []
    for model in SOURCES:
        original = source / model["file"]
        if not original.exists():
            request = urllib.request.Request(model["url"], headers={"User-Agent": "ToonPro-reference-review"})
            with urllib.request.urlopen(request, timeout=60) as response:
                original.write_bytes(response.read())
        data = original.read_bytes()
        if hashlib.sha256(data).hexdigest() != model["sha256"]:
            raise ValueError("Pinned source checksum differs: " + str(original))
        prepared, count, resized = prepare(data, model.get("textureLimit", 0))
        destination = ready / model["file"]
        destination.write_bytes(prepared)
        entry = dict(model, sourcePath=str(original.resolve()), readyPath=str(destination.resolve()),
                             readySha256=hashlib.sha256(prepared).hexdigest(), readyBytes=len(prepared),
                             identityTextureTransformsRemoved=count,
                             resizedImages=resized,
                             preparation="Remove identity texture transforms; resample explicitly listed textures; preserve geometry/rig/morph payloads" if count or resized else "Unmodified original VRM")
        if args.canonical_tool:
            args.resource_dir.mkdir(parents=True, exist_ok=True)
            resource = args.resource_dir / (Path(model["file"]).stem + "-v1.toon3dasset")
            report = ready / (Path(model["file"]).stem + "-canonical-report.json")
            command = [str(args.canonical_tool.resolve()), str(destination.resolve()), str(resource.resolve()),
                       "--name", model["name"], "--catalog-id", model["id"], "--source-url", model["url"], "--report", str(report.resolve())]
            if model.get("yaw180"):
                command.append("--yaw-180")
            for name in model.get("excludedMeshNames", []):
                command.extend(["--exclude-mesh", name])
            subprocess.run(command, check=True)
            entry["canonical"] = json.loads(report.read_text())
        manifest.append(entry)
    (args.output_dir / "anime-preparation-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.canonical_tool:
        records = []
        for item in manifest:
            canonical = dict(item["canonical"])
            canonical["output"] = Path(canonical["output"]).name
            canonical.pop("input", None)
            records.append(dict(id=item["id"], name=item["name"], sourceUrl=item["url"],
                                sourceRevision=item["url"].split("/")[5],
                                sourceSha256=item["sha256"], readySha256=item["readySha256"],
                                identityTextureTransformsRemoved=item["identityTextureTransformsRemoved"],
                                resizedImages=item["resizedImages"], canonical=canonical))
        document = dict(schema=1, preparationScript="scripts/prepare_reference3d_anime.py",
                        canonicalTool="tests/reference3d_prepare_asset.cpp", models=records)
        (args.resource_dir / "anime-sources.json").write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
