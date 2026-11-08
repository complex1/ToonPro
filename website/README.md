# Toon Pro website

A new static product website and interactive documentation app for the current **0.76.0 source build**. It replaces the former website implementation. The landing page, detailed manual, feature explorer, setup guide and comparison all distinguish current development from published **0.75.0** packages.

## Build and preview

The builder and static checks need only **Python 3.10+**, with no Python packages, Node runtime or build service required. Run from the repository root:

```sh
python3 website/build.py --output build/website/ToonPro --base /ToonPro/
python3 website/check_site.py --root build/website/ToonPro --base /ToonPro/
python3 -m http.server 8000 --bind 127.0.0.1 --directory build/website
```

Open [the local website](http://127.0.0.1:8000/ToonPro/). Pass `--base /` for root hosting. `--origin https://example.com` changes canonical URLs and the sitemap; it does not deploy anything. Output belongs in an isolated generated directory; the builder refreshes its known route and asset directories on each build.

## Pages and interactions

- **Overview:** real app screenshots, a keyboard-accessible workspace tour, and a small ink playground with selectable colors, paired brush-size controls, clear and local PNG download. The playground is explicitly a browser illustration, not the native brush engine.
- **Documentation:** 35 structured chapters with setup, step-by-step workflows, control behavior, limits, source references, screenshots where relevant, adjacent chapters, a table of contents and reading progress. Category filters and text search help browsing.
- **Features:** a visual showcase of 10 actual app captures, plus 141 documented capabilities. Every entry has a related screenshot thumbnail and an expandable larger image, behavior, workflow and limits. Captions distinguish exact controls from broader workspace context. Filter by category/text, expand matching entries, save features locally, and copy a direct link to an entry. Local storage failures fall back to preferences for the current visit.
- **Setup:** requirements, keyboard-accessible macOS/Windows/Linux tabs, build commands and copy controls. Recipes do not imply all platforms have passed current acceptance.
- **Compare:** specific workflow strengths and tradeoffs against five existing tools, with official sources and research date.
- **Credits:** capture provenance, locally hosted font licenses, playground scope and privacy.

Global search uses a locally generated index, supports Cmd/Ctrl+K, arrows, Enter and Escape, and returns focus after closing. Screenshot dialogs support enlargement and keyboard dismissal. Navigation collapses on mobile; motion respects `prefers-reduced-motion`. Icon help uses the shared 600 ms hover delay. No application backend, analytics, accounts or cookies are needed.

Reading text uses a 17–18 px desktop / 16 px mobile scale, with larger navigation and clear caption/metadata sizes. Space Grotesk handles reading and controls; Bricolage Grotesque supplies expressive display headings. Both font files and their SIL OFL notices are bundled locally from the [Google Fonts repository](https://github.com/google/fonts).

## Source map

| File | Purpose |
| --- | --- |
| `build.py` | Standard-library static renderer; escaped structured text, base-path routing, search, sitemap and build metadata |
| `data/manual.json` | Complete chapter and feature content; current source status and technical evidence |
| `data/feature-media.json` | Explicit chapter-to-capture mappings, context labels and visual showcase entries |
| `assets/site.css` | Readable typography, bright semantic text, responsive layouts and reduced-motion behavior |
| `assets/feature-media.css` | Feature showcase, thumbnail and expanded screenshot presentation |
| `assets/site.js` | Search, tabs, filters, saved features, dialogs, copying, reading progress and ink playground |
| `assets/fonts/` | Locally hosted Space Grotesk / Bricolage Grotesque and SIL OFL notices |
| `assets/screenshots/current/` | Fresh app captures and provenance manifest |
| `check_site.py` | Output crawler and content/provenance checks |
| `test_site.py` | Browser interaction, route and responsive checks |

Keep `data/manual.json` grounded in the actual source and technical docs. New feature entries require an existing chapter, useful workflow and explicit limits. `available` means implemented in the current source, `development` highlights newer local behavior, and `optional` requires a runtime or build capability; none means universal platform acceptance.

## Refresh real screenshots

Build the app and capture targets first, then run:

```sh
python3 scripts/capture_website.py
```

See that script’s `--help` for capture paths and options. It runs the application’s real Qt capture fixtures, converts the images to WebP with `cwebp`, and records dimensions, source fixtures, hashes, timestamps and captions. Sample imported illustrations must be identified as imported artwork. Keep capture masters and logs in ignored `build/`; publish only the curated images and manifest. Never invent UI or use a generated mockup as an application screenshot.

## Browser verification

Use an isolated Python environment with Playwright and an installed Chromium/Chrome. No artist browser profile is needed:

```sh
python3 -m venv build/website-tools
build/website-tools/bin/python -m pip install playwright
build/website-tools/bin/python website/test_site.py \
  --base-url http://127.0.0.1:8000/ToonPro/ \
  --output build/website/ToonPro \
  --report-dir build/website-browser-qa \
  --chrome "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
```

On Windows/Linux supply the corresponding Chrome/Chromium executable. Keep browser artifacts in `build/`. The current application and website results are recorded in [the testing report](../docs/testing-2026-11-08.md).

## Publication

Published and verified on **8 November 2026**: source `ab8103925ef707897f7b37847c7be8c4e3b0b891`, 82 matching public files and all ten live browser check groups passing. The live site exposes `deployment.json` and `deployment-files.json` for source/file provenance.

The repository uses a manual GitHub Pages deployment: **prebuilt files on the `gh-pages` branch, at `/`, with `.nojekyll`**. The public URL is [Toon Pro](https://complex1.github.io/ToonPro/), so generate the site with `--base /ToonPro/`. GitHub Actions stays disabled; this publication path does not run or publish desktop release candidates. The retained `pages.yml` is an optional manual build/validation workflow only.

For a website update:

1. Commit and push the reviewed application/website source to `main`.
2. Build the site from that exact source revision into an isolated generated directory and run `check_site.py` against it. Run the browser checks when presentation or interactions change.
3. Commit only the generated website files to `gh-pages`, preserving its existing history. Include `.nojekyll`, and record the source revision in `deployment.json`.
4. Push `gh-pages`, then inspect GitHub Pages status and fetch the live pages/assets. Match the live deployment metadata to the intended source revision before reporting success.

Never force-push a deployment branch or commit SDKs, local build caches, credentials or app packages as website assets. Building locally alone does not publish; the reviewed generated output must be pushed to the configured Pages source. The current publication record lives in [the development log](../docs/DEVELOPMENT.md).
