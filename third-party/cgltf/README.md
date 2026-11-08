# cgltf

Vendored unmodified `cgltf.h` from cgltf **v1.15**, commit
`360db1a95480fe102ae9c69b27c5d101167ff5ba`.

- Upstream: https://github.com/jkuhlmann/cgltf/tree/360db1a95480fe102ae9c69b27c5d101167ff5ba
- License: MIT; retained in `LICENSE` and the header.
- `cgltf.h` SHA-256: `e378a21c084bf1f288bb799de827bb26906efb024255f1ecf1705ea13f11c6ec`
- `LICENSE` SHA-256: `f619925f80ef862497aaf8e8155ef218fa6a2190055129523ca3df9119a9ba95`

`src/reference3d_import.cpp` compiles the implementation once. Toon Pro supplies
bounded allocation and local resource loading; cgltf's unrestricted file loader
is not used. Import policy and unsupported features are diagnosed by Toon Pro.
