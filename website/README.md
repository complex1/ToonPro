# Toon Pro website

An entirely static product site and illustrated artist manual for GitHub Pages. All fonts, artwork, icons and screenshots are served locally. Search runs in the browser; no analytics, accounts, database or tracking cookies are used.

## Build and preview

Use Python 3.12 or later in an isolated environment:

```sh
python -m pip install -r website/requirements.txt
python website/build.py --output build/website-preview/ToonPro --base /ToonPro/
python website/check_site.py --root build/website-preview/ToonPro --base /ToonPro/
python -m http.server 8765 --bind 127.0.0.1 --directory build/website-preview
```

Open `http://127.0.0.1:8765/ToonPro/`. For another repository name or root hosting, pass its corresponding `--base`. `--origin` sets canonical and sitemap origins.

## Content and assets

- `templates/home.html`, `assets/site.css` and `assets/site.js`: landing page and shared presentation.
- `content/*.md`: artist guides, with YAML title, description, category and order. The builder adds sidebar, table of contents, primary screenshots and adjacent-guide navigation. Links to another `.md` guide become page links.
- `assets/screenshots/index.json`: actual capture provenance, captions, dimensions and hashes. The `slug` associates a primary screenshot with a guide. Supplemental figures live beside the relevant steps in Markdown.
- `assets/ARTWORK.txt`: original illustration brief and provenance. The image is illustrative art, not an automated application feature. The workspace screenshot shows it genuinely imported into the app.
- `releases.json`: platform-specific availability and verified download URLs. Keep URLs null until a public artifact exists. Never turn a pending/failed build into a download button.
- `check_site.py`: output-level checks for routes, fragments, assets, screenshots, search coverage and public-path hygiene.

The large original PNG is preserved locally as an artwork master; the optimized WebP is the published asset. Fonts retain their OFL texts. Screenshot masters and browser QA evidence remain in `build/`.

## Publishing

`.github/workflows/pages.yml` builds, validates and deploys on website changes to `main`. In repository **Settings → Pages**, choose **GitHub Actions** once. The workflow's standard token can deploy an enabled Pages site; it does not have permission to enable that repository setting. GitHub Pages serves the site at `https://complex1.github.io/ToonPro/` after successful deployment.

Application CI is separate from website CI. Linux distribution is deferred; the site offers only Windows/Mac candidates when verified public artifacts are available. Source availability does not set a pricing policy or an application license.
