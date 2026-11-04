"""Populate a staged site registry from the public, verified desktop preview."""
import argparse
import hashlib
import json
import os
import re
import urllib.request
from pathlib import Path


REPOSITORY = "complex1/ToonPro"
WEB = f"https://github.com/{REPOSITORY}"
API = f"https://api.github.com/repos/{REPOSITORY}"
TARGETS = {
    "macos-arm64": ("mac", "dmg", "Includes the 3D renderer. Ad-hoc signed Mac preview."),
    "macos-x86_64": ("intel", "dmg", "Drawing and animation for Intel Macs. Ad-hoc signed Mac preview."),
    "windows-x86_64": ("windows", "zip", "Unsigned Windows preview. Extract the ZIP and open bin/ToonPro.exe."),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "ToonPro-Pages"})
    if url.startswith(API + "/") and os.environ.get("GH_TOKEN"):
        # Never send the API credential to a redirected release asset host.
        request.add_unredirected_header("Authorization", "Bearer " + os.environ["GH_TOKEN"])
    with urllib.request.urlopen(request, timeout=30) as response:
        body = response.read(8 * 1024 * 1024 + 1)
    require(len(body) <= 8 * 1024 * 1024, "Unexpectedly large release metadata")
    return body


def public_previews(version):
    page = 1
    pattern = re.compile(rf"v{re.escape(version)}-preview\.[1-9][0-9]*")
    while True:
        releases = json.loads(fetch(f"{API}/releases?per_page=20&page={page}"))
        for release in releases:
            if (release["prerelease"] and not release["draft"] and release["published_at"]
                    and pattern.fullmatch(release["tag_name"])):
                yield release
        if len(releases) < 20:
            return
        page += 1


def verified_cards(version, release):
    tag = release["tag_name"]
    url = f"{WEB}/releases/download/{tag}/"
    assets = {item["name"]: item for item in release["assets"]}
    require(len(assets) == len(release["assets"]), "Duplicate release assets")

    def asset(name):
        item = assets[name]
        require(item["state"] == "uploaded" and item["size"] > 0
                and item["browser_download_url"] == url + name
                and re.fullmatch(r"sha256:[0-9a-f]{64}", item["digest"] or ""),
                f"Unverified release asset {name}")
        return item

    def metadata(name):
        item = asset(name)
        data = fetch(item["browser_download_url"])
        require(len(data) == item["size"] and "sha256:" + hashlib.sha256(data).hexdigest() == item["digest"],
                f"Downloaded metadata differs from GitHub's digest for {name}")
        return data

    manifest = json.loads(metadata("release-manifest.json"))
    require(manifest["schemaVersion"] == 1 and manifest["version"] == version
            and manifest["tag"] == tag == f"v{version}-preview.{manifest['runId']}"
            and manifest["sourceCommit"] == release["target_commitish"]
            and re.fullmatch(r"[0-9a-f]{40}", manifest["sourceCommit"])
            and manifest["channel"] == "preview" and manifest["productionReady"] is False
            and manifest["fullNativeTestsAndRelocatedRuntimePassed"] is True and manifest["platforms"],
            "Release identity or native verification is missing")
    checksums = {}
    for line in metadata("SHA256SUMS").decode().splitlines():
        digest, name = line.split("  ", 1)
        require(name not in checksums and asset(name)["digest"] == "sha256:" + digest,
                f"Invalid published checksum for {name}")
        checksums[name] = digest
    require(set(checksums) == set(assets) - {"SHA256SUMS"}, "Release checksums are incomplete")

    cards = {}
    for platform in manifest["platforms"]:
        target = platform["target"]
        require(target in TARGETS and target not in cards, f"Unexpected platform {target}")
        _, primary_format, note = TARGETS[target]
        prefix = f"ToonPro-{version}-{target}"
        expected = {prefix + ".zip"}
        if primary_format == "dmg":
            expected.add(prefix + ".dmg")
        files = platform["artifacts"]
        require(len(files) == len(expected) and {file["file"] for file in files} == expected,
                f"Invalid distribution archive names for {target}")
        for file in files:
            item = asset(file["file"])
            require(file["url"] == item["browser_download_url"] and file["bytes"] == item["size"]
                    and file["sha256"] == checksums[file["file"]], f"Archive mismatch for {file['file']}")
        sources = prefix + "-dependency-sources.tar.gz"
        require(platform["dependencySources"] == sources and sources in checksums, f"Missing sources for {target}")
        primary = asset(f"{prefix}.{primary_format}")
        links = []
        if primary_format == "dmg":
            links.append({"label": "ZIP alternative", "url": url + prefix + ".zip"})
        links += [{"label": "Checksums", "url": url + "SHA256SUMS"},
                  {"label": "Dependency sources", "url": url + sources}]
        cards[target] = dict(status="available", note=note, url=primary["browser_download_url"],
                             sha256=checksums[primary["name"]], bytes=primary["size"], links=links)
    return cards


def sync(registry, releases):
    version = registry["version"]
    available = {}
    used_tags = []
    # GitHub lists newest releases first. Inspect an older manifest only when
    # its public asset list offers a platform we still need.
    for release in releases:
        names = {item["name"] for item in release["assets"]}
        offered = {target for target, (_, fmt, _) in TARGETS.items()
                   if f"ToonPro-{version}-{target}.{fmt}" in names}
        missing = offered - available.keys()
        if missing:
            cards = verified_cards(version, release)
            require(offered == cards.keys(), "Platform archives disagree with the verified manifest")
            available.update({target: cards[target] for target in missing})
            used_tags.append(release["tag_name"])
        if len(available) == len(TARGETS):
            break
    for target, (key, _, _) in TARGETS.items():
        card = registry[key]
        if target in available:
            card.update(available[target])
        else:
            if card.get("status") == "available":
                card.update(status="testing", note="A verified preview for this platform is pending.")
            card.update(url=None, links=[])
            card.pop("sha256", None)
            card.pop("bytes", None)
    registry["releaseUrl"] = None
    if used_tags:
        registry["releaseUrl"] = f"{WEB}/releases/tag/{used_tags[0]}" if len(used_tags) == 1 else f"{WEB}/releases"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("registry", type=Path, help="Registry in the Pages build checkout")
    path = parser.parse_args().registry
    registry = json.loads(path.read_text(encoding="utf-8"))
    sync(registry, public_previews(registry["version"]))
    path.write_text(json.dumps(registry, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(registry["releaseUrl"] or "No public preview for this version yet")
