# MakeHuman/MPFB graphical assets

The experimental reference body/head/hand library derives its anatomical base
from MakeHuman Community's MPFB core graphical assets. Toon Pro's parameter
mapping, model-family mixes and pose recipes are independent implementation.

- Upstream: <https://github.com/makehumancommunity/mpfb2>
- Pinned revision: `6ff1eb358f6c1959ced77e79e6dd2406d9a71f49`
- Asset license: **CC0 1.0 Universal**; see [LICENSE.CC0.txt](LICENSE.CC0.txt).
- Upstream's distinction between graphical data and program code:
  <https://github.com/makehumancommunity/mpfb2/blob/6ff1eb358f6c1959ced77e79e6dd2406d9a71f49/LICENSE.md>
- Project license explanation: <https://www.makehumancommunity.org/about/license.html>

Upstream explicitly includes meshes, targets, rigs, poses and JSON mesh data in
the CC0 graphical-asset license. MPFB program code is GPLv3; MakeHuman program
code has its own AGPL license. Their program code is not incorporated here.

The runtime uses a bounded, prepared local asset resource. No Blender/MPFB
installation, account, network request or generation service is needed to use
the models. Preparation scripts, resource provenance and the
[model-library record](../../docs/3d-model-library.md) document conversions,
counts, source hashes, skeleton remapping and offline influence limits.

No Clip Studio Paint, CELSYS, Tripo or VRoid model data is included in these
built-in model families. The model library remains behind
`TOON_ENABLE_EXPERIMENTAL_3D`, OFF by default, pending artist review.
