# Toon Pro 0.75.0 release verification

Updated 3 November 2026. This is a test-candidate release, not a production approval. The artist authorized public source and unsigned test downloads in `complex1/ToonPro`; no store submission or GitHub Release has been published.

## In-progress verification

The optimized macOS arm64 package has passed relocated offscreen and native Cocoa runtime checks. Linux and Windows build checks are in progress. Intel Mac has compiled and passed relocated runtime checks under Rosetta, but H.264/AAC export intermittently raises SIGILL, including an actual DMG installation-copy test. Its artifact is withheld pending investigation and real Intel validation.

The final full macOS regression run passed **247/247 suites** in **310.71 seconds**. The first pass found two stale migration/panel-count expectations and missing help on generated controls; both the fixtures and shared control help have been fixed and rechecked. Logs are preserved under `build/release-075-*`.

See [packaging](packaging.md) for reproducible commands, runtime/dependency coverage, source archives and current platform limitations. Final artifact paths, checksums and exact test results will be recorded here when the running checks finish.
