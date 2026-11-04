"""Check the public-release boundary used by Pages without contacting GitHub."""
import hashlib
import json
import unittest
from unittest.mock import patch

import sync_downloads as sync


VERSION = "0.75.0"
TAG = "v0.75.0-preview.123"
BASE = f"{sync.WEB}/releases/download/{TAG}/"
COMMIT = "a" * 40


def fixture(targets):
    payloads = {}
    platforms = []
    for target in targets:
        prefix = f"ToonPro-{VERSION}-{target}"
        formats = ["zip"] if target.startswith("windows") else ["dmg", "zip"]
        artifacts = []
        for suffix in formats:
            name = prefix + "." + suffix
            payloads[name] = name.encode()
            artifacts.append({"file": name, "sha256": hashlib.sha256(payloads[name]).hexdigest(),
                              "bytes": len(payloads[name]), "url": BASE + name})
        sources = prefix + "-dependency-sources.tar.gz"
        payloads[sources] = b"source bundle"
        platforms.append({"target": target, "artifacts": artifacts, "dependencySources": sources})
    manifest = {"schemaVersion": 1, "version": VERSION, "tag": TAG, "runId": "123",
                "sourceCommit": COMMIT, "channel": "preview", "productionReady": False,
                "fullNativeTestsAndRelocatedRuntimePassed": True, "platforms": platforms}
    payloads["release-manifest.json"] = json.dumps(manifest).encode()
    payloads["SHA256SUMS"] = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n"
                                     for name, data in payloads.items()).encode()
    assets = [{"name": name, "state": "uploaded", "browser_download_url": BASE + name,
               "size": len(data), "digest": "sha256:" + hashlib.sha256(data).hexdigest()}
              for name, data in payloads.items()]
    return {"assets": assets, "tag_name": TAG, "target_commitish": COMMIT}, payloads


class PublicDownloads(unittest.TestCase):
    def registry(self):
        return {"version": VERSION, "mac": {}, "windows": {}, "intel": {}}

    def apply(self, registry, release, payloads):
        with patch.object(sync, "fetch", side_effect=lambda url: payloads[url.removeprefix(BASE)]):
            sync.sync(registry, release)

    def test_refresh_and_withdrawn_target(self):
        registry = self.registry()
        self.apply(registry, *fixture(sync.TARGETS))
        self.assertTrue(registry["mac"]["url"].endswith(".dmg"))
        self.assertEqual(len(registry["intel"]["links"]), 3)
        self.assertTrue(registry["windows"]["url"].endswith(".zip"))
        self.apply(registry, *fixture(["windows-x86_64"]))
        self.assertIsNone(registry["mac"]["url"])
        self.assertEqual(registry["intel"]["links"], [])
        self.assertTrue(registry["windows"]["bytes"] > 0)
        self.assertEqual(registry["releaseUrl"], f"{sync.WEB}/releases/tag/{TAG}")

    def test_modified_manifest_or_github_size_prevents_publication(self):
        release, payloads = fixture(["windows-x86_64"])
        with self.assertRaisesRegex(ValueError, "metadata differs"):
            self.apply(self.registry(), release, dict(payloads, **{"release-manifest.json": b"{}"}))
        release["assets"][0]["size"] += 1
        with self.assertRaisesRegex(ValueError, "Archive mismatch"):
            self.apply(self.registry(), release, payloads)

    def test_lookup_ignores_unpublished_and_other_versions(self):
        base = {"prerelease": True, "draft": False, "published_at": "2026-11-04", "tag_name": TAG}
        releases = [dict(base, draft=True), dict(base, tag_name="v0.74.0-preview.234"), base]
        with patch.object(sync, "fetch", return_value=json.dumps(releases).encode()):
            self.assertEqual(sync.latest_preview(VERSION), base)
        with patch.object(sync, "fetch", return_value=b"[]"):
            self.assertIsNone(sync.latest_preview(VERSION))


if __name__ == "__main__":
    unittest.main()
