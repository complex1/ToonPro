#!/usr/bin/env python3
"""Validate a generated Toon Pro site without fetching external resources.

Checks the published output, not template implementation details. External release
availability is deliberately not inferred: advertised artifacts must match the
explicit release registry, and remote availability needs separate release checks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urljoin, urlsplit
import xml.etree.ElementTree as ET

PRIVATE_PATH = re.compile(r"file://|/(?:Users|home|private/var|var/folders)/|[A-Za-z]:[\\/](?:Users|Windows|Program Files)[\\/]", re.I)
TEXT_SUFFIXES = {'.html', '.css', '.js', '.json', '.xml', '.txt', '.md', '.svg'}
VOID_TAGS = {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr'}


@dataclass
class Element:
    tag: str
    attrs: dict[str, str]
    line: int
    text: str = ''


class Page(HTMLParser):
    def __init__(self, path: Path):
        super().__init__(convert_charrefs=True)
        self.path = path
        self.elements: list[Element] = []
        self.ids: set[str] = set()
        self.duplicate_ids: set[str] = set()
        self.stack: list[Element] = []
        self.feed(path.read_text(encoding='utf-8'))
        self.close()

    def handle_starttag(self, tag, attrs):
        element = Element(tag, dict((key, value or '') for key, value in attrs), self.getpos()[0])
        self.elements.append(element)
        identifier = element.attrs.get('id')
        if identifier:
            if identifier in self.ids:
                self.duplicate_ids.add(identifier)
            self.ids.add(identifier)
        if tag not in VOID_TAGS:
            self.stack.append(element)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VOID_TAGS:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, -1, -1):
            if self.stack[index].tag == tag:
                del self.stack[index:]
                break

    def handle_data(self, data):
        for element in self.stack:
            element.text += data


@dataclass
class Result:
    errors: list[str] = field(default_factory=list)
    counts: dict[str, int] = field(default_factory=dict)

    def require(self, condition, message):
        if not condition:
            self.errors.append(message)


def check_site(root: Path, base: str, release_path: Path | None = None,
               expected_guides: int = 16, expected_screenshots: int = 23) -> Result:
    root = root.resolve()
    result = Result()
    result.require(root.is_dir(), f'Site output is missing: {root}')
    result.require(base.startswith('/') and base.endswith('/') and '..' not in base,
                   f'Invalid base URL: {base}')
    if result.errors:
        return result
    pages = {path.resolve(): Page(path) for path in root.rglob('*.html')}
    result.counts['pages'] = len(pages)
    result.require(len(pages) == expected_guides + 5,
                   f'Expected {expected_guides + 5} HTML pages, found {len(pages)}')
    origins = set()
    for page in pages.values():
        for e in page.elements:
            if e.tag == 'link' and e.attrs.get('rel') == 'canonical':
                parsed = urlsplit(e.attrs.get('href', ''))
                if parsed.netloc:
                    origins.add(parsed.netloc)
    references_checked = 0

    def reference(raw: str, source: Path, context: str, *, fragment=True):
        nonlocal references_checked
        references_checked += 1
        raw = raw.strip()
        if not raw:
            result.errors.append(f'{context}: empty URL')
            return
        if PRIVATE_PATH.search(raw):
            result.errors.append(f'{context}: private/local URL {raw!r}')
            return
        parsed = urlsplit(raw)
        if parsed.scheme in {'mailto', 'tel'}:
            return
        if parsed.scheme == 'data':
            return
        if parsed.scheme and parsed.scheme not in {'http', 'https'}:
            result.errors.append(f'{context}: unsupported URL scheme {parsed.scheme!r}')
            return
        if parsed.netloc and parsed.netloc not in origins:
            return
        current_url = base + source.relative_to(root).as_posix()
        resolved = urlsplit(urljoin(current_url, raw))
        path_url = unquote(resolved.path)
        if not path_url.startswith(base):
            result.errors.append(f'{context}: URL escapes site base {raw!r}')
            return
        target = (root / path_url[len(base):]).resolve()
        if not target.is_relative_to(root):
            result.errors.append(f'{context}: URL escapes output directory {raw!r}')
            return
        if target.is_dir():
            target /= 'index.html'
        if not target.is_file():
            result.errors.append(f'{context}: missing target {raw!r}')
            return
        if fragment and resolved.fragment:
            identifier = unquote(resolved.fragment)
            if target.suffix == '.html' and identifier not in pages[target].ids:
                result.errors.append(f'{context}: missing fragment {raw!r}')

    for path, page in pages.items():
        label = path.relative_to(root).as_posix()
        result.require(not page.duplicate_ids, f'{label}: duplicate IDs {sorted(page.duplicate_ids)}')
        headings = [e for e in page.elements if e.tag == 'h1']
        result.require(len(headings) == 1 and bool(headings[0].text.strip()), f'{label}: must have one nonempty h1')
        mains = [e for e in page.elements if e.tag == 'main']
        result.require(len(mains) == 1 and mains[0].attrs.get('id') == 'main', f'{label}: missing unique main landmark')
        result.require(any(e.tag == 'html' and e.attrs.get('lang') for e in page.elements), f'{label}: missing document language')
        result.require(any(e.tag == 'title' and e.text.strip() for e in page.elements), f'{label}: missing title')
        for e in page.elements:
            context = f'{label}:{e.line} <{e.tag}>'
            for attr in ('href', 'src', 'poster'):
                if attr in e.attrs:
                    reference(e.attrs[attr], path, context + ' ' + attr)
            if 'srcset' in e.attrs:
                for candidate in e.attrs['srcset'].split(','):
                    reference(candidate.strip().split()[0], path, context + ' srcset')
            for attr in ('aria-controls', 'aria-labelledby', 'aria-describedby'):
                for identifier in e.attrs.get(attr, '').split():
                    result.require(identifier in page.ids, f'{context}: {attr} target {identifier!r} is missing')
            if e.tag == 'img':
                result.require('alt' in e.attrs, f'{context}: image has no alt attribute')
                # Decorative marks/icons and an unloaded dialog placeholder have
                # no content alternative. Actual screenshots/artwork must have one.
                src = e.attrs.get('src', '')
                decorative = '/assets/icons/' in src or src.endswith('/assets/mark.svg')
                if src and not decorative:
                    result.require(bool(e.attrs.get('alt', '').strip()), f'{context}: content image needs nonempty alt text')
            if e.tag == 'button':
                result.require(bool(e.text.strip() or e.attrs.get('aria-label') or e.attrs.get('aria-labelledby')),
                               f'{context}: button has no accessible name')

    for path in root.rglob('*'):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
            continue
        text = path.read_text(encoding='utf-8')
        result.require(not PRIVATE_PATH.search(text), f'{path.relative_to(root)}: private filesystem path leaked into public output')
        if path.suffix == '.css':
            for raw in re.findall(r'url\(\s*[\'\"]?([^\'\"\)]+)[\'\"]?\s*\)', text):
                reference(raw, path, f'{path.relative_to(root)} CSS url', fragment=False)

    def read_json(name):
        path = root / name
        try:
            return json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError) as error:
            result.errors.append(f'{name}: cannot read JSON ({error})')
            return None

    info = read_json('build-info.json')
    if info:
        result.require(info.get('guideCount') == expected_guides, 'build-info.json: incorrect guide count')
        result.require(info.get('screenshotCount') == expected_screenshots, 'build-info.json: incorrect screenshot count')
        result.require(info.get('basePath') == base, 'build-info.json: base path mismatch')
    guide_paths = {path.parent.name for path in pages if path.parent.parent == root / 'docs'}
    result.counts['guides'] = len(guide_paths)
    result.require(len(guide_paths) == expected_guides, f'Expected {expected_guides} guide pages, found {len(guide_paths)}')
    search = read_json('search-index.json')
    if isinstance(search, list):
        covered = set()
        for entry in search:
            reference(entry.get('url', ''), root / 'index.html', 'search-index.json entry')
            result.require(bool(entry.get('title', '').strip()), 'search-index.json: empty result title')
            if not urlsplit(entry.get('url', '')).fragment:
                covered.add(urlsplit(entry.get('url', '')).path.rstrip('/').split('/')[-1])
        result.require(guide_paths <= covered, f'Search omits guides: {sorted(guide_paths - covered)}')
        result.counts['searchEntries'] = len(search)
    shots = read_json('assets/screenshots/index.json')
    if isinstance(shots, dict):
        entries = shots.get('screenshots', [])
        files = {path.name for path in (root / 'assets/screenshots').glob('*.webp')}
        result.counts['screenshots'] = len(files)
        result.require(len(entries) == expected_screenshots and len(files) == expected_screenshots,
                       f'Expected {expected_screenshots} screenshot entries/files, found {len(entries)}/{len(files)}')
        result.require(len({e['slug'] for e in entries}) == len(entries), 'Screenshot slugs are not unique')
        result.require(guide_paths <= {e['slug'] for e in entries}, 'A guide has no documented primary screenshot')
        result.require(files == {e['file'] for e in entries}, 'Screenshot manifest and shipped WebP files differ')
        for entry in entries:
            path = root / 'assets/screenshots' / entry['file']
            result.require(bool(entry.get('alt', '').strip()) and bool(entry.get('caption', '').strip()),
                           f'Screenshot {entry["file"]}: missing caption or alt')
            if path.is_file():
                data = path.read_bytes()
                result.require(len(data) == entry.get('bytes'), f'Screenshot {path.name}: size does not match manifest')
                result.require(hashlib.sha256(data).hexdigest() == entry.get('sha256'), f'Screenshot {path.name}: checksum does not match manifest')
        result.require(not list((root / 'assets/screenshots').glob('*.png')), 'PNG screenshot masters should not ship in the public site')
    sitemap = root / 'sitemap.xml'
    try:
        xml = ET.parse(sitemap)
        locations = [e.text for e in xml.iter() if e.tag.endswith('}loc')]
        result.require(len(locations) == expected_guides + 4, 'Sitemap page count is incorrect')
        for location in locations:
            reference(location or '', root / 'index.html', 'sitemap.xml')
    except (OSError, ET.ParseError) as error:
        result.errors.append(f'sitemap.xml: cannot read ({error})')
    if release_path:
        registry = json.loads(release_path.read_text())
        approved = {r['url'] for r in registry.values() if isinstance(r, dict) and r.get('url')}
        advertised = set()
        for page in pages.values():
            for e in page.elements:
                href = e.attrs.get('href', '')
                parsed = urlsplit(href)
                if e.tag == 'a' and (parsed.path.lower().endswith(('.zip', '.dmg', '.exe', '.msi', '.tar.gz'))
                                     or '/releases/download/' in parsed.path
                                     or re.match(r'Download for (?:Mac|Windows)\b', e.text.strip(), re.I)):
                    advertised.add(href)
                    result.require(href in approved, f'{page.path.name}: unapproved/unavailable download link {href}')
                    result.require(parsed.scheme == 'https', f'Download must use HTTPS: {href}')
        result.require(approved <= advertised, f'Available downloads missing from site: {sorted(approved - advertised)}')
        result.counts['advertisedDownloads'] = len(advertised)
    result.counts['referencesChecked'] = references_checked
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--root', type=Path, dest='output_root')
    parser.add_argument('--base', default='/ToonPro/')
    parser.add_argument('--releases', type=Path, default=Path(__file__).with_name('releases.json'))
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    output = args.output_root or args.output
    if output is None:
        parser.error('provide --root or the output directory')
    result = check_site(output, args.base, args.releases)
    report = {'passed': not result.errors, **result.counts, 'errors': result.errors,
              'externalDownloadAvailability': 'Not fetched; advertised URLs checked against explicit release registry.'}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 1 if result.errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
