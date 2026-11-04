# Packaging notices

The 32 SPDX license and exception texts in `v3.27.0.zip` are byte-for-byte copies of files under [SPDX license-list-data v3.27.0/text](https://github.com/spdx/license-list-data/tree/v3.27.0/text). Their filenames and Git blob hashes were checked against the upstream tag. The additional [ICU 73.2 complete notice](https://github.com/unicode-org/icu/blob/release-73-2/icu4c/LICENSE) matches the SHA-256 recorded in `scripts/package_desktop.py`.

The bundle lets clean release builds stage the notices without network access. The packager selects the named texts required by the installed SDK manifests, and records the original sources and individual SHA-256 hashes in each package. When updating the SDK, check the requested filenames against the upstream sources and update this bundle if needed.
