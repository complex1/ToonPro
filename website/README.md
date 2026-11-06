# Toon Pro website

An entirely static product site and illustrated artist manual for GitHub Pages. It explains the drawing-first purpose, lets visitors explore the project model, and compares workflows using linked official sources. All fonts, artwork, icons and screenshots are served locally. Search runs in the browser; no analytics, accounts, database or tracking cookies are used.

## Build and preview

Use Python 3.12 or later in an isolated environment:

```sh
python -m pip install -r website/requirements.txt
python website/build.py --output build/product-website/ToonPro --base /ToonPro/
python website/check_site.py --root build/product-website/ToonPro --base /ToonPro/
python -m http.server 8765 --bind 127.0.0.1 --directory build/product-website
```

Open `http://127.0.0.1:8765/ToonPro/`. For another repository name or root hosting, pass its corresponding `--base`. `--origin` sets canonical and sitemap origins.

## Content and assets

- `templates/home.html`: the visitor journey, three project-type tabs, real app captures, a user-controlled twelve-frame illustration, tool exploration and learning paths.
- `templates/vision.html` and `templates/compare.html`: purpose, intended audience, boundaries and five sourced product comparisons. Keep comparisons specific to documented workflows; do not imply measured superiority or unverified feature parity.
- `assets/site.css` / `assets/site.js`: shared presentation, local search, screenshot previews, menus and tool tabs. `assets/product.css` / `assets/product.js` add the product pages, keyboard-accessible project/comparison tabs, reading progress and the illustrative shot. The demo never autoplays and pauses when hidden or offscreen.
- `content/*.md`: artist guides, with YAML title, description, category and order. Add `status: development` and a `version` label for a local-only workflow. Other guides default to public preview 0.75. The builder adds version notices, sidebar, table of contents, primary screenshots and adjacent-guide navigation. Links to another `.md` guide become page links; root-relative links respect `--base`.
- `assets/screenshots/index.json`: actual capture provenance, captions, dimensions and hashes. The `slug` associates a primary screenshot with a guide. Supplemental figures live beside the relevant steps in Markdown.
- `assets/ARTWORK.txt`: original illustration brief and provenance. The image is illustrative art, not an automated application feature. The workspace screenshot shows it genuinely imported into the app.
- `releases.json` supplies platform labels and the locally previewable download registry. In the Pages build, `sync_downloads.py` reads the public GitHub prerelease for this version. It verifies the manifest, checksums, platform package names, byte counts and GitHub asset digests before enabling a button. To refresh your local preview, run `python website/sync_downloads.py website/releases.json` before building.
- `check_site.py`: output-level checks for routes, fragments, assets, screenshots, search coverage and public-path hygiene.
- `test_site.py`: browser regression coverage for search, tabs, the shot illustration, screenshot dialogs, menus, guide navigation and responsive route/image checks. Browser validation requires an available Chrome installation and Playwright. Reports belong in `build/`, not the published assets.

The site contains 17 artist guides and 7 other pages. The managed Projects / drawing tabs / Assets guide describes **local 0.76 development**, while the public download registry remains **0.75.0**. Actual captures of the local app are labeled accordingly. The paper-plane shot is a teaching illustration, not an editor capture or a generated-animation feature.

CSS and JavaScript URLs include content hashes so a returning visitor receives the matching presentation and interaction code after a site update.

Purpose and comparison source documents live in [`docs/product-vision.md`](../docs/product-vision.md) and [`docs/product-comparison.md`](../docs/product-comparison.md). Refresh the source document and website together when claims or scope change. Re-check linked official product documentation before changing market comparisons; the current research date is 6 November 2026.

The large original PNG is preserved locally as an artwork master; the optimized WebP is the published asset. Fonts retain their OFL texts. Screenshot masters and browser QA evidence remain in `build/`.

## Publishing

**GitHub Actions is currently disabled by repository policy. Website work and verification stay local/manual.** The checked-in `.github/workflows/pages.yml` records the previous Pages build/deploy configuration; editing source or building locally does not deploy it. Do not enable Actions as a side effect of content work.

Build and validate the exact output intended for publication and verify the public release registry. Publication to the existing GitHub Pages host needs an authorized deployment route while Actions remains disabled; this content update does not configure an alternative route. Preserve the `/ToonPro/` base path. The public address is `https://complex1.github.io/ToonPro/`; a local preview does not establish that the public site has changed. Check the live pages separately after an authorized deployment. Do not create a second hosting identity to bypass the manual-release policy.

Application CI is separate from website CI. Linux distribution is deferred; the site offers only Windows/Mac candidates when verified public artifacts are available. Source availability does not set a pricing policy or an application license.
