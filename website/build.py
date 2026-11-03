#!/usr/bin/env python3
"""Build the public Toon Pro site. No remote content, account or backend needed."""
from __future__ import annotations
import argparse
import html
import json
import math
import re
import shutil
from pathlib import Path
from urllib.parse import quote
import markdown
import yaml

ROOT = Path(__file__).resolve().parent
SLUGS = {'workspaces':'drawing-workspace','brushes':'brushes-and-dynamics',
         'character-models':'references-and-characters','cel-paint':'cel-paint-and-light-table',
         'light-table':'cel-paint-and-light-table','3d-reference':'3d-reference-and-rigging',
         'frame-animation':'frame-by-frame-animation','keyframes':'keyframes-cameras-and-effects',
         'symbols':'symbols-and-lip-sync','audio-export':'audio-movie-and-export',
         'shortcuts':'shortcuts-and-troubleshooting'}

def esc(value): return html.escape(str(value), quote=True)

def read_guides():
    guides=[]
    for path in sorted((ROOT/'content').glob('*.md')):
        text=path.read_text()
        if not text.startswith('---\n'): continue
        _, header, body=text.split('---',2)
        data=yaml.safe_load(header)
        if not all(k in data for k in ('title','description','category','order')):
            raise ValueError(f'Missing guide metadata: {path}')
        data.update(slug=path.stem, body=body.strip(), minutes=max(2,math.ceil(len(body.split())/210)))
        guides.append(data)
    return sorted(guides,key=lambda g:g['order'])

def build(out, base, origin):
    if not base.startswith('/') or not base.endswith('/') or '..' in base: raise ValueError('Base must be an absolute directory URL')
    guides=read_guides()
    if not guides: raise ValueError('No guides to publish')
    out.mkdir(parents=True,exist_ok=True)
    # PNG artwork master is preserved locally; only the optimized WebP ships.
    shutil.copytree(ROOT/'assets',out/'assets',dirs_exist_ok=True,ignore=shutil.ignore_patterns('*.png'))
    shots=json.loads((ROOT/'assets/screenshots/index.json').read_text())
    screenshots={s['slug']:s for s in shots['screenshots']}
    release=json.loads((ROOT/'releases.json').read_text())
    categories=list(dict.fromkeys(g['category'] for g in guides))
    def url(path=''): return base+path.lstrip('/')
    def icon(name): return f'<img src="{url("assets/icons/"+name+".svg")}" alt="" width="20" height="20">'
    def header(active=''):
        nav=[('Workflow',url('#workflow'),'workflow'),('Features',url('#features'),'features'),('Field guide',url('docs/'),'docs')]
        links=''.join(f'<a href="{href}"'+(' aria-current="page"' if active==key else '')+f'>{name}</a>' for name,href,key in nav)
        return f'''<a class="skip-link" href="#main">Skip to content</a><header class="site-header"><div class="wrap header-inner"><a class="brand" href="{base}" aria-label="Toon Pro home"><img src="{url('assets/mark.svg')}" alt="" width="32" height="32"><span class="brand-name">TOON<span>PRO</span></span></a><span class="preview-badge">0.75 / PREVIEW</span><nav class="primary-nav" id="primary-nav" aria-label="Main navigation">{links}</nav><div class="header-actions"><button class="icon-button" data-search aria-label="Search the field guide" data-tooltip="Search guides (Ctrl / ⌘ K)">{icon('search')}</button><a class="header-download" href="{url('download/')}">Get Toon Pro ↗</a><button class="icon-button menu-toggle" aria-label="Open navigation" aria-expanded="false" aria-controls="primary-nav">☰</button></div></div></header>'''
    footer=f'''<footer class="site-footer"><div class="wrap"><div class="footer-top"><a class="brand" href="{base}"><img src="{url('assets/mark.svg')}" alt="" width="28" height="28"><span class="brand-name">TOON<span>PRO</span></span></a><nav class="footer-links" aria-label="Footer"><a href="{url('docs/')}">Field guide</a><a href="{url('download/')}">Downloads</a><a href="https://github.com/complex1/ToonPro">GitHub ↗</a><a href="{url('credits/')}">Credits</a></nav></div><div class="footer-bottom"><span>© 2026 Toon Pro. Independent tools for hand-drawn stories.</span><span>PUBLIC PREVIEW · WINDOWS + macOS · NO TRACKING COOKIES</span></div></div></footer>'''
    dialogs='''<dialog id="search-dialog" class="search-dialog" aria-label="Search the field guide"><div class="search-head"><label for="doc-search" class="sr-only">Search guides</label><input id="doc-search" type="search" placeholder="Try “brush pressure”, “lip sync” or “export”…" autocomplete="off" aria-controls="search-results"><button data-close-search aria-label="Close search">Esc</button></div><div id="search-results" class="search-results"></div><p class="sr-only" id="search-announcement" aria-live="polite"></p><div class="search-help">↑ ↓ to explore · Enter to open · Esc to close</div></dialog><dialog id="image-dialog" class="image-dialog" aria-label="Enlarged screenshot"><div class="image-toolbar"><figcaption id="image-caption"></figcaption><button data-close-image aria-label="Close enlarged screenshot">×</button></div><img id="enlarged-image" alt=""></dialog>'''
    def page(path,title,description,body,active='',kind=''):
        target=out/path if path.endswith('.html') else out/path/'index.html'
        target.parent.mkdir(parents=True,exist_ok=True)
        canonical=origin.rstrip('/')+url('' if path=='index.html' else path)
        text=f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>{esc(title)} · Toon Pro</title><meta name="description" content="{esc(description)}"><meta name="theme-color" content="#0c1011"><meta name="color-scheme" content="dark"><link rel="canonical" href="{canonical}"><meta property="og:title" content="{esc(title)} · Toon Pro"><meta property="og:description" content="{esc(description)}"><meta property="og:type" content="website"><meta property="og:url" content="{canonical}"><meta property="og:image" content="{origin.rstrip('/')+url('assets/art/blue-hour.webp')}"><meta name="twitter:card" content="summary_large_image"><link rel="icon" href="{url('assets/mark.svg')}" type="image/svg+xml"><link rel="preload" href="{url('assets/fonts/display.ttf')}" as="font" type="font/ttf" crossorigin><link rel="stylesheet" href="{url('assets/site.css')}"><script defer src="{url('assets/site.js')}"></script></head><body data-base="{base}" data-page="{kind}">{header(active)}{body}{footer}{dialogs}</body></html>'''
        target.write_text(text)
    home=(ROOT/'templates/home.html').read_text().replace('{{BASE}}',base)
    for old,new in SLUGS.items(): home=home.replace('/docs/'+old+'/', '/docs/'+new+'/')
    for old,new in {'workspace':'drawing-workspace','brushes':'brushes-and-dynamics','character-models':'references-and-characters','timeline':'frame-by-frame-animation'}.items():
        home=home.replace('/screenshots/'+old+'.webp','/screenshots/'+new+'.webp')
    home=home.replace('ACTUAL APPLICATION CAPTURE · SAMPLE DOCUMENT','ACTUAL APP · ORIGINAL ILLUSTRATION IMPORTED INTO TOON PRO')
    page('index.html','Your world. Frame by frame.','Draw, color and animate your story in Toon Pro. A native desktop workspace for hand-drawn anime, with practical illustrated guides.',home,kind='home')
    cards=''
    for category in categories:
        group=''
        for g in guides:
            if g['category']!=category: continue
            group+=f'<a class="guide-card" href="{url("docs/"+g["slug"]+"/")}"><span>{g["minutes"]} MIN READ · ILLUSTRATED GUIDE</span><h3>{esc(g["title"])}</h3><p>{esc(g["description"])}</p><strong>Open guide ↗</strong></a>'
        cards+=f'<section class="guide-group"><h2>{esc(category.upper())}</h2><div class="guide-grid">{group}</div></section>'
    docs=f'''<main id="main"><section class="docs-banner"><div class="wrap"><p class="eyebrow">THE TOON PRO FIELD GUIDE</p><h1>MAKE YOURSELF<br><em>AT HOME.</em></h1><p>Practical steps, real panels, and the details that matter. Start with a drawing or find your way through a complete animated shot.</p><button class="search-trigger-wide" data-search>Search tools, techniques and workflows <kbd>⌘ / Ctrl K</kbd></button></div></section><div class="wrap docs-index">{cards}</div></main>'''
    page('docs/','The field guide','Detailed, illustrated Toon Pro documentation for drawing, brushes, character workflows, animation, 3D references and export.',docs,'docs','docs-index')
    search=[]
    for i,g in enumerate(guides):
        md=markdown.Markdown(extensions=['extra','toc','sane_lists'],extension_configs={'toc':{'permalink':'#','permalink_title':'Link to this section','toc_depth':'2-3'}})
        prose=md.convert(g['body'])
        prose=re.sub(r'href="([^":/#]+)\.md(#[^"]*)?"',lambda m:f'href="{url("docs/"+m[1]+"/")}{m[2] or ""}"',prose)
        prose=prose.replace('src="/assets/','src="'+url('assets/'))
        prose=prose.replace('<img ','<img loading="lazy" data-zoom ')
        shot=screenshots.get(g['slug'])
        hero=''
        if shot:
            hero=f'<figure class="doc-hero"><img src="{url("assets/screenshots/"+shot["file"])}" width="{shot["width"]}" height="{shot["height"]}" alt="{esc(shot["alt"])}" data-zoom><figcaption>{esc(shot["caption"])} <span>Actual application capture; sample document.</span></figcaption></figure>'
        nav=f'<a class="sidebar-home" href="{url("docs/")}">← All guides</a>'
        for category in categories:
            nav+=f'<p class="sidebar-group">{esc(category)}</p>'
            for other in guides:
                if other['category']==category:
                    current=' aria-current="page"' if other['slug']==g['slug'] else ''
                    nav+=f'<a href="{url("docs/"+other["slug"]+"/")}"{current}>{esc(other["title"])}</a>'
        previous=guides[i-1] if i else None; following=guides[i+1] if i+1<len(guides) else None
        neighbors=''
        for item,label in [(previous,'PREVIOUS GUIDE'),(following,'NEXT GUIDE')]:
            neighbors+=(f'<a href="{url("docs/"+item["slug"]+"/")}"><small>{label}</small>{esc(item["title"])} ↗</a>' if item else '<span></span>')
        article=f'''<button class="docs-mobile-toggle" aria-expanded="false" aria-controls="docs-sidebar"><span>Browse the field guide</span><span aria-hidden="true">＋</span></button><div class="docs-shell"><aside id="docs-sidebar" class="docs-sidebar"><nav aria-label="Guide navigation">{nav}</nav></aside><main id="main" class="doc-article"><div class="breadcrumbs"><a href="{url('docs/')}">Field guide</a><span>/</span>{esc(g['category'])}</div><h1>{esc(g['title'])}</h1><p class="doc-description">{esc(g['description'])}</p><div class="doc-meta"><span>{g['minutes']} MIN READ</span><span>TOON PRO 0.75</span><button class="print-button" data-print>Print / save PDF</button></div>{hero}<div class="doc-prose">{prose}</div><nav class="doc-next" aria-label="Adjacent guides">{neighbors}</nav><p class="doc-source">Based on the implemented public-preview workflow. <a href="https://github.com/complex1/ToonPro/blob/main/website/content/{g['slug']}.md">View this guide's source ↗</a></p></main><aside class="doc-toc" aria-label="On this page"><p>ON THIS PAGE</p>{md.toc}</aside></div>'''
        page('docs/'+g['slug']+'/',g['title'],g['description'],article,'docs','guide')
        plain=html.unescape(re.sub('<[^>]+>',' ',prose)); plain=re.sub(r'\s+',' ',plain)
        search.append({'title':g['title'],'description':g['description'],'category':g['category'],'url':url('docs/'+g['slug']+'/'),'text':plain})
        for heading in re.findall(r'<h[23] id="([^"]+)">(.*?)</h[23]>',prose):
            label=html.unescape(re.sub('<[^>]+>','',heading[1])).rstrip('#').strip()
            search.append({'title':label,'description':g['title'],'category':g['category'],'url':url('docs/'+g['slug']+'/')+'#'+heading[0],'text':g['description']})
    (out/'search-index.json').write_text(json.dumps(search,ensure_ascii=False))
    download_cards=''
    for key,symbol in [('mac','⌘'),('windows','⊞')]:
        r=release[key]
        action=f'<a class="button primary" href="{esc(r["url"])}">Download {"for Mac" if key=="mac" else "for Windows"} <span>↓</span></a>' if r.get('url') else f'<span class="status-label">{"PUBLIC DOWNLOAD IN PREPARATION" if key=="mac" else "VALIDATION IN PROGRESS"}</span>'
        download_cards+=f'<article class="download-card"><span class="os-icon" aria-hidden="true">{symbol}</span><h2>{esc(r["label"])}</h2><p class="minimum">{esc(r["minimum"])}</p><p>{esc(r["note"])}</p>{action}</article>'
    download=f'''<main id="main" class="simple-page wrap"><p class="eyebrow">TOON PRO 0.75 / PUBLIC PREVIEW</p><h1>YOUR WORKSPACE.<br><em>YOUR NEXT STORY.</em></h1><p class="simple-intro">Get the desktop app for Windows or macOS. Preview builds are for trying the workflow and sharing feedback; they are not signed production releases.</p><div class="download-grid">{download_cards}</div><p class="download-note">3D rendering is currently available on Apple Silicon Mac. Intel Mac downloads are withheld while an audio-export crash is investigated. Linux distribution is deferred.</p><div class="download-links"><a class="text-link" href="https://github.com/complex1/ToonPro/actions">Follow build progress ↗</a><a class="text-link" href="{url('docs/getting-started/')}">Read the first-project guide ↗</a><a class="text-link" href="https://github.com/complex1/ToonPro/issues">Report an issue ↗</a></div><section class="faq"><h2>BEFORE YOUR FIRST FRAME.</h2><details><summary>What should I know about the preview?</summary><p>Save a copy of an important project before testing a new build. Mac candidates are ad-hoc signed and not notarized; Windows candidates are unsigned. Package checks cover saved projects, resources, drawing, review playback and media export. Physical tablet feel and real production projects still need artist testing.</p></details><details><summary>Can I use a pen display or drawing tablet?</summary><p>Toon Pro supports tablet input and pressure-aware brush controls. Start with a small ink brush, then test pressure, fast curves, slow strokes and the move from panels to the canvas. Hardware and driver behavior varies; the troubleshooting guide explains how to isolate an issue.</p></details><details><summary>Where are the source and release notes?</summary><p>The source and current verification notes are available in the public <a href="https://github.com/complex1/ToonPro">GitHub repository</a>. Public source availability does not set a license or a future pricing policy. Refer to repository terms and the actual package notices.</p></details><details><summary>Do I need an account to read the guides?</summary><p>No. This site has no sign-in, analytics or tracking cookies. Search runs locally in your browser. GitHub may require an account to download temporary Actions artifacts; direct release links will appear here when available.</p></details></section></main>'''
    page('download/','Get Toon Pro','Windows and Mac preview availability, requirements and honest build status for Toon Pro.',download,kind='download')
    credits=f'''<main id="main" class="simple-page wrap"><p class="eyebrow">THE DETAILS BEHIND THE DETAILS</p><h1>MADE WITH<br><em>CARE & CREDIT.</em></h1><div class="credit-list"><h2>Original illustrative artwork</h2><p>“Blue Hour”, the city illustration on this website, was created for Toon Pro using the Image Generation tool. It is illustrative concept art, not a claim that the app generated or painted the image. The drawing-workspace capture shows that image imported as an artwork layer in the real application. The matching character turnaround was also created with Image Generation and imported into the actual Model Sheets panel.</p><a href="{url('assets/ARTWORK.txt')}">Artwork brief and provenance ↗</a><h2>Real application screenshots</h2><p>The field guide uses real Toon Pro application and widget captures with sample documents. Some panels are captured in focused fixtures to keep their controls legible. Capture versions and source fixtures are recorded in the screenshot index; these are not fabricated interfaces.</p><a href="{url('assets/screenshots/index.json')}">Screenshot provenance index ↗</a><h2>Type & icons</h2><p>Barlow Condensed and Manrope are locally hosted under the SIL Open Font License. Lucide icons retain the repository's existing attribution.</p><p><a href="{url('assets/fonts/barlowcondensed-OFL.txt')}">Barlow Condensed license</a> · <a href="{url('assets/fonts/manrope-OFL.txt')}">Manrope license</a> · <a href="{url('assets/icons/NOTICE.md')}">Icon attribution</a></p><h2>Privacy</h2><p>This static website has no accounts, forms, tracking cookies or analytics. Guide search runs in the browser. External GitHub links follow GitHub's own terms and privacy practices.</p></div></main>'''
    page('credits/','Credits and provenance','Original illustration, real application captures, fonts, icons and privacy information.',credits,kind='credits')
    page('404.html','This frame is missing','Return to Toon Pro or find the guide you need.',f'<main id="main" class="simple-page wrap"><p class="eyebrow">404 / MISSING FRAME</p><h1>BACK TO<br><em>THE DRAWING BOARD.</em></h1><p class="simple-intro">That page may have moved. The field guide can help you find the next step.</p><p style="margin-top:30px"><a class="button primary" href="{url("docs/")}">Open the field guide ↗</a></p></main>')
    urls=[base,url('docs/'),url('download/'),url('credits/')]+[url('docs/'+g['slug']+'/') for g in guides]
    (out/'sitemap.xml').write_text('<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'+''.join(f'<url><loc>{esc(origin.rstrip("/")+u)}</loc></url>' for u in urls)+'</urlset>')
    (out/'robots.txt').write_text(f'User-agent: *\nAllow: /\nSitemap: {origin.rstrip("/")+url("sitemap.xml")}\n')
    (out/'.nojekyll').touch()
    (out/'build-info.json').write_text(json.dumps({'guideCount':len(guides),'guideWords':sum(len(g['body'].split()) for g in guides),'screenshotCount':len(shots['screenshots']),'version':release['version'],'basePath':base},indent=2))
    print(f'Built {len(guides)} guides and 5 site pages in {out}')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'dist');parser.add_argument('--base',default='/ToonPro/');parser.add_argument('--origin',default='https://complex1.github.io');args=parser.parse_args()
    build(args.output.resolve(),args.base,args.origin)
