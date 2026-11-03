# Toon Pro 0.75.0 release verification

Updated 3 November 2026. This is a test-candidate release, not a production approval. The artist authorized public source and unsigned test downloads in `complex1/ToonPro`; no store submission or GitHub Release has been published.

## In-progress verification

The optimized macOS arm64 package has passed relocated offscreen and native Cocoa runtime checks. Linux and Windows build checks are in progress. Intel Mac has compiled and passed three clean runtime checks under Rosetta after an initial unexplained SIGILL during encoding; final packaging and native Intel acceptance remain separate.

The full macOS regression run passed 244/247 suites on its first pass. Two stale migration/panel-count expectations have been corrected and pass. The remaining tooltip regression is being investigated before the candidate is finalized. Logs are preserved under `build/release-075-*`.

See [packaging](packaging.md) for reproducible commands, runtime/dependency coverage, source archives and current platform limitations. Final artifact paths, checksums and exact test results will be recorded here when the running checks finish.
