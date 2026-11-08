#!/usr/bin/env python3
"""Crawl built pages, assets, search routes and capture evidence. Standard library only."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urljoin, urlsplit

SOURCE = Path(__file__).resolve().parent
VOID = set('area base br col embed hr img input link meta param source track wbr'.split())


class Page(HTMLParser):
    def __init__(self, path):
        super().__init__(convert_charrefs=True)
        self.elements, self.ids, self.duplicates, self.stack = [], set(), set(), []
        self.feed(path.read_text(encoding='utf-8'))

    def handle_starttag(self, tag, attrs):
        element = {'tag': tag, 'attrs': dict(attrs), 'text': '', 'line': self.getpos()[0],
                   'parent': self.stack[-1] if self.stack else None}
        self.elements.append(element)
        identifier = element['attrs'].get('id')
        if identifier:
            if identifier in self.ids: self.duplicates.add(identifier)
            self.ids.add(identifier)
        if tag not in VOID: self.stack.append(element)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID: self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for i in range(len(self.stack)-1, -1, -1):
            if self.stack[i]['tag'] == tag:
                del self.stack[i:]
                break

    def handle_data(self, text):
        for element in self.stack: element['text'] += text


def check_site(root, base):
    root = root.resolve()
    errors, counts = [], {}
    def require(condition, message):
        if not condition: errors.append(message)
    require(root.is_dir(), 'Output directory is missing')
    require(bool(re.fullmatch(r'/(?:[A-Za-z0-9_-]+/)*', base)), 'Invalid base URL')
    if errors: return {'passed': False, 'counts': counts, 'errors': errors}
    data = json.loads((SOURCE/'data/manual.json').read_text())
    pages = {p.resolve(): Page(p) for p in root.rglob('*.html')}
    origins = {urlsplit(e['attrs'].get('href', '')).netloc for p in pages.values()
               for e in p.elements if e['attrs'].get('rel') == 'canonical'}
    counts.update(pages=len(pages), chapters=len(data['chapters']), features=len(data['features']), references=0)
    require(len(pages) == len(data['chapters'])+7, 'HTML page count does not match manual plus six routes and 404')

    def reference(raw, source, context, fragment=True):
        counts['references'] += 1
        if not raw: require(False, context+': empty URL'); return
        parsed = urlsplit(raw)
        if parsed.scheme in ('data', 'mailto', 'tel'): return
        if parsed.scheme not in ('', 'http', 'https'):
            require(False, context+': unsupported URL scheme'); return
        if parsed.netloc and parsed.netloc not in origins: return
        current = base+source.relative_to(root).as_posix()
        resolved = urlsplit(urljoin(current, raw))
        urlpath = unquote(resolved.path)
        if not urlpath.startswith(base): require(False, context+': escapes site base '+raw); return
        target = (root/urlpath[len(base):]).resolve()
        if not target.is_relative_to(root): require(False, context+': escapes output '+raw); return
        if target.is_dir(): target /= 'index.html'
        require(target.is_file(), context+': missing '+raw)
        if fragment and resolved.fragment and target in pages:
            require(unquote(resolved.fragment) in pages[target].ids, context+': missing fragment '+raw)

    for path, page in pages.items():
        label = path.relative_to(root).as_posix()
        require(not page.duplicates, label+': duplicate IDs '+str(sorted(page.duplicates)))
        h1 = [e for e in page.elements if e['tag'] == 'h1']
        require(len(h1) == 1 and bool(h1[0]['text'].strip()), label+': expected one nonempty H1')
        mains = [e for e in page.elements if e['tag'] == 'main']
        require(len(mains) == 1 and mains[0]['attrs'].get('id') == 'main', label+': expected unique main landmark')
        require(any(e['tag'] == 'title' and e['text'].strip() for e in page.elements), label+': missing title')
        require(any(e['tag'] == 'html' and e['attrs'].get('lang') == 'en' for e in page.elements), label+': missing language')
        for e in page.elements:
            a, tag = e['attrs'], e['tag']
            context = f'{label}:{e["line"]} <{tag}>'
            for attr in ('href', 'src', 'poster'):
                if attr in a: reference(a[attr], path, context)
            for identifier in ' '.join(a.get(x) or '' for x in ('aria-controls','aria-labelledby','aria-describedby')).split():
                require(identifier in page.ids, context+': missing ARIA target '+identifier)
            if tag == 'img':
                require('alt' in a, context+': missing alt attribute')
                if a.get('src') and not a['src'].endswith('/assets/mark.svg'):
                    require(bool(a.get('alt', '').strip()), context+': empty image alternative')
            if tag == 'button': require(bool(e['text'].strip() or a.get('aria-label') or a.get('aria-labelledby')), context+': unnamed button')
            if tag == 'h2': require(bool(e['text'].strip()), context+': empty heading')
        require('capture-pending' not in path.read_text(), label+': unfinished screenshot placeholder')

    for path in root.rglob('*.css'):
        for raw in re.findall(r'url\(\s*[\"\']?([^\"\')]+)', path.read_text()): reference(raw.strip(), path, path.name)
    for route in ('', 'docs', 'features', 'setup', 'compare', 'credits'):
        require((root/route/'index.html').is_file(), 'Missing route '+route)
    for chapter in data['chapters']: require((root/'docs'/chapter['id']/'index.html').is_file(), 'Missing chapter '+chapter['id'])
    feature_page = pages.get((root/'features/index.html').resolve())
    if feature_page:
        actual = {e['attrs'].get('data-feature') for e in feature_page.elements if 'data-feature' in e['attrs']}
        require(actual == {f['id'] for f in data['features']}, 'Feature output differs from manual catalog')
    search = json.loads((root/'search-index.json').read_text())
    counts['searchEntries'] = len(search)
    require(len(search) == len(data['chapters'])+len(data['features'])+3, 'Search index count differs from manual')
    for entry in search:
        require(all(isinstance(entry.get(k), str) and entry[k].strip() for k in ('title','text','category','url')), 'Malformed search entry')
        reference(entry.get('url'), root/'index.html', 'Search: '+entry.get('title',''))
    manifest = json.loads((root/'assets/screenshots/current/index.json').read_text())
    shots = manifest.get('screenshots', [])
    counts['screenshots'] = len(shots)
    require(bool(shots), 'No actual application screenshots')
    require(all(manifest.get(k) for k in ('application','version','capturedAt','method','captureCommand')), 'Capture manifest lacks build/capture provenance')
    slugs = [s.get('slug') for s in shots]
    require(len(set(slugs)) == len(slugs), 'Duplicate capture slugs')
    for shot in shots:
        file = (root/'assets/screenshots/current'/shot['file']).resolve()
        require(file.is_relative_to(root/'assets/screenshots/current'), 'Capture path escapes directory')
        require(file.is_file(), 'Missing capture '+shot['file'])
        if file.is_file(): require(hashlib.sha256(file.read_bytes()).hexdigest() == shot.get('sha256'), 'Capture hash mismatch '+shot['file'])
        require(all(isinstance(shot.get(k), int) and shot[k] > 0 for k in ('width','height')), 'Missing capture dimensions '+shot['file'])
        require(bool(shot.get('provenance') and shot.get('source')), 'Missing capture provenance '+shot['file'])
    if feature_page:
        media = json.loads((SOURCE/'data/feature-media.json').read_text())
        capture_files = {shot['slug']: shot['file'] for shot in shots}
        def has_class(element, name):
            return name in (element['attrs'].get('class') or '').split()
        def within(element, ancestor):
            while element:
                if element is ancestor: return True
                element = element['parent']
            return False
        def descendants(ancestor, tag=None, css_class=None):
            return [e for e in feature_page.elements if within(e, ancestor)
                    and (tag is None or e['tag'] == tag)
                    and (css_class is None or has_class(e, css_class))]
        feature_rows = {e['attrs']['data-feature']: e for e in feature_page.elements
                        if 'data-feature' in e['attrs']}
        for feature in data['features']:
            row = feature_rows.get(feature['id'])
            if row is None: continue  # Catalog equality above reports missing rows.
            slug = media.get('chapters', {}).get(feature['chapter'], {}).get('slug')
            require(slug in capture_files, feature['id']+': missing mapped application capture')
            require(row['attrs'].get('data-capture') == slug, feature['id']+': capture mapping differs from chapter')
            thumbnails = descendants(row, tag='img', css_class='feature-thumb')
            require(len(thumbnails) == 1, feature['id']+': expected a visible-summary screenshot thumbnail')
            details = descendants(row, css_class='feature-detail')
            captures = [capture for detail in details for capture in descendants(detail, css_class='app-capture')]
            require(bool(captures), feature['id']+': expanded feature has no application screenshot')
            detail_images = [image for capture in captures for image in descendants(capture, tag='img')]
            require(bool(detail_images), feature['id']+': expanded screenshot has no image')
            for image in thumbnails + detail_images:
                filename = Path(unquote(urlsplit(image['attrs'].get('src', '')).path)).name
                require(filename == capture_files.get(slug), feature['id']+': screenshot does not match its mapped feature chapter')
                require(all(str(image['attrs'].get(k, '')).isdigit() and int(image['attrs'][k]) > 0
                            for k in ('width', 'height')), feature['id']+': screenshot lacks intrinsic dimensions')
        intros = [e for e in feature_page.elements if has_class(e, 'feature-intro')]
        require(any(descendants(e, css_class='app-capture') for e in intros), 'Feature introduction lacks an application screenshot')
        showcases = [e for e in feature_page.elements if has_class(e, 'feature-showcase')]
        gallery_images = [image for showcase in showcases
                          for capture in descendants(showcase, css_class='app-capture')
                          for image in descendants(capture, tag='img')]
        require(len(gallery_images) == len(media.get('gallery', [])) and bool(gallery_images),
                'Feature showcase does not include all gallery screenshot previews')
        require({Path(unquote(urlsplit(image['attrs'].get('src', '')).path)).name for image in gallery_images}
                == {capture_files.get(item.get('slug')) for item in media.get('gallery', [])},
                'Feature showcase screenshots differ from gallery mappings')
        counts['featureThumbnails'] = sum(e['tag'] == 'img' and has_class(e, 'feature-thumb') for e in feature_page.elements)
        counts['featureGalleryImages'] = len(gallery_images)
    font_faces = '\n'.join(path.read_text() for path in root.rglob('*.css'))
    for family in ('Bricolage Grotesque', 'Space Grotesk'):
        compact_family = re.sub(r'[^a-z]', '', family.lower())
        definitions = [definition for definition in re.findall(r'@font-face\s*\{([^}]+)\}', font_faces, re.I)
                       if any(compact_family == re.sub(r'[^a-z]', '', value.lower())
                              for value in re.findall(r'font-family\s*:\s*([^;]+)', definition, re.I))]
        require(bool(definitions), 'Missing self-hosted font-face '+family)
        require(any(not urlsplit(raw).scheme and not urlsplit(raw).netloc
                    for definition in definitions for raw in re.findall(r'url\(\s*[\"\']?([^\"\')]+)', definition, re.I)),
                'Font is not self-hosted: '+family)
    info = json.loads((root/'build-info.json').read_text())
    for key, value in {'chapterCount':len(data['chapters']), 'featureCount':len(data['features']), 'screenshotCount':len(shots), 'pageCount':len(pages)-1, 'basePath':base}.items():
        require(info.get(key) == value, 'Build metadata mismatch '+key)
    require((root/'.nojekyll').exists(), 'Missing GitHub Pages .nojekyll marker')
    return {'passed': not errors, 'counts': counts, 'errors': errors}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--base', default='/ToonPro/')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try: report = check_site(args.root, args.base)
    except (OSError, ValueError, KeyError) as error: report = {'passed':False, 'counts':{}, 'errors':[str(error)]}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if report['passed'] else 1)
