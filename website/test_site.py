#!/usr/bin/env python3
"""Exercise the published site in an isolated, real Chrome browser profile.

Run with the repository's website-tools Python (Playwright installed). No browser
is downloaded, no artist profile is opened, and no remote service is contacted.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from urllib.parse import urljoin, urlsplit
from playwright.sync_api import sync_playwright, expect


def test_site(base_url: str, output: Path, report_dir: Path, chrome: str):
    report_dir.mkdir(parents=True, exist_ok=True)
    origin = urlsplit(base_url).netloc
    checks, problems, console_errors, requests = [], [], [], []
    paths = sorted(path.relative_to(output).as_posix() for path in output.rglob('*.html'))
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(executable_path=chrome, headless=True)
        context = browser.new_context(viewport={'width': 1440, 'height': 1000}, reduced_motion='reduce')
        page = context.new_page()
        page.on('pageerror', lambda error: console_errors.append(str(error)))
        page.on('console', lambda message: console_errors.append(message.text) if message.type == 'error' else None)
        page.on('response', lambda response: requests.append({'url': response.url, 'status': response.status})
                if urlsplit(response.url).netloc == origin and response.status >= 400 else None)

        def check(name, action):
            try:
                action()
                checks.append({'name': name, 'passed': True})
                print('PASS: ' + name, flush=True)
            except Exception as error:
                problems.append(f'{name}: {error}')
                print(f'FAIL: {name}: {error}', flush=True)
                checks.append({'name': name, 'passed': False, 'error': str(error)})
                page.screenshot(path=str(report_dir / f'failure-{len(problems)}.png'), full_page=True)

        def home(width=1440):
            page.set_viewport_size({'width': width, 'height': 1000 if width > 600 else 844})
            page.goto(base_url, wait_until='networkidle')

        def search_keyboard():
            home()
            trigger = page.get_by_role('button', name='Search the field guide', exact=True)
            trigger.focus()
            page.keyboard.press('Control+k')
            dialog = page.get_by_role('dialog', name='Search the field guide')
            expect(dialog).to_be_visible()
            field = dialog.get_by_role('searchbox', name='Search guides')
            expect(field).to_be_focused()
            field.fill('lip sync')
            matches = dialog.get_by_role('link')
            expect(matches.first).to_contain_text(re.compile('lip', re.I))
            assert matches.count() > 0
            destination = matches.first.get_attribute('href')
            assert destination and '/docs/' in destination
            page.keyboard.press('ArrowDown')
            page.keyboard.press('Enter')
            page.wait_for_url(urljoin(base_url, destination))
            expect(page.get_by_role('heading', level=1)).to_be_visible()
            assert 'lip' in page.locator('main').inner_text().lower()
            # Escape must give keyboard users their originating control back.
            trigger = page.get_by_role('button', name='Search the field guide', exact=True)
            trigger.click()
            expect(page.get_by_role('dialog', name='Search the field guide')).to_be_visible()
            page.keyboard.press('Escape')
            expect(page.get_by_role('dialog', name='Search the field guide')).not_to_be_visible()
            expect(trigger).to_be_focused()
            assert not page.locator('body').evaluate("e => getComputedStyle(e).overflow === 'hidden'")

        def search_empty_and_punctuation():
            home()
            page.get_by_role('button', name='Search the field guide', exact=True).click()
            dialog = page.get_by_role('dialog', name='Search the field guide')
            field = dialog.get_by_role('searchbox', name='Search guides')
            field.fill('no-such-tool-987654321')
            expect(dialog.locator('.search-status').filter(has_text='No results for')).to_be_visible()
            assert dialog.get_by_role('link').count() == 0
            field.fill('<img src=x onerror=alert(1)>')
            expect(dialog.locator('.search-status').filter(has_text='No results for')).to_be_visible()
            assert dialog.locator('img').count() == 0
            field.fill('brush pressure')
            expect(dialog.get_by_role('link').first).to_contain_text(re.compile('brush', re.I))
            dialog.get_by_role('button', name='Close search').click()
            expect(dialog).not_to_be_visible()

        def search_immediate_enter():
            home()
            page.get_by_role('button', name='Search the field guide', exact=True).click()
            dialog = page.get_by_role('dialog', name='Search the field guide')
            field = dialog.get_by_role('searchbox', name='Search guides')
            expect(dialog.get_by_role('link').first).to_be_visible()
            # Enter immediately after input must not open stale suggestions.
            field.fill('no-such-tool-987654321')
            page.keyboard.press('Enter')
            expect(dialog.locator('.search-status').filter(has_text='No results for')).to_be_visible()
            assert urlsplit(page.url).path == urlsplit(base_url).path
            # The same shortcut must also work when the old query had no links.
            field.fill('lip sync')
            page.keyboard.press('Enter')
            page.wait_for_url(urljoin(base_url, 'docs/symbols-and-lip-sync/'))
            expect(page.get_by_role('heading', level=1)).to_contain_text(re.compile('lip', re.I))

        def workflow_tabs():
            home()
            tablist = page.get_by_role('tablist', name='Production workflow')
            tabs = tablist.get_by_role('tab')
            assert tabs.count() == 4
            first = tabs.nth(0)
            first.focus()
            old_title = page.get_by_role('tabpanel').get_by_role('heading').inner_text()
            for key, expected_index in [('ArrowRight', 1), ('End', 3), ('ArrowRight', 0), ('ArrowLeft', 3), ('Home', 0)]:
                page.keyboard.press(key)
                expect(tabs.nth(expected_index)).to_be_focused()
                expect(tabs.nth(expected_index)).to_have_attribute('aria-selected', 'true')
                assert tablist.locator('[aria-selected="true"]').count() == 1
                panel = page.get_by_role('tabpanel')
                expect(panel).to_have_attribute('aria-labelledby', tabs.nth(expected_index).get_attribute('id'))
                image = panel.locator('img')
                expect(image).to_have_attribute('alt', re.compile('.+'))
                expect(image).to_be_visible()
                image.evaluate('async e => { if (!e.complete) await e.decode(); }')
                assert image.evaluate('e => e.naturalWidth') > 0
            tabs.nth(2).click()
            panel = page.get_by_role('tabpanel')
            assert panel.get_by_role('heading').inner_text() != old_title
            panel.get_by_role('link').click()
            expect(page.get_by_role('heading', level=1)).to_contain_text('Frame')

        def image_dialog_keyboard():
            home()
            image = page.get_by_role('button', name='Enlarge: Actual Toon Pro drawing workspace', exact=False)
            expect(image).to_have_count(1)
            image.focus()
            page.keyboard.press('Enter')
            dialog = page.get_by_role('dialog', name='Enlarged screenshot')
            expect(dialog).to_be_visible()
            enlarged = dialog.locator('img')
            assert enlarged.get_attribute('src') == image.get_attribute('src') or enlarged.get_attribute('src') == image.evaluate('e => e.src')
            assert enlarged.get_attribute('alt').strip()
            for _ in range(4):
                page.keyboard.press('Tab')
                assert dialog.evaluate('e => !document.hasFocus() || e.contains(document.activeElement)'), 'Screenshot modal leaked keyboard focus into the background page'
            page.keyboard.press('Escape')
            expect(dialog).not_to_be_visible()
            expect(image).to_be_focused()
            page.get_by_role('button', name='Enlarge workspace screenshot', exact=True).click()
            expect(dialog).to_be_visible()
            dialog.get_by_role('button', name='Close enlarged screenshot').click()
            expect(page.get_by_role('button', name='Enlarge workspace screenshot', exact=True)).to_be_focused()

        def mobile_navigation():
            home(390)
            toggle = page.get_by_role('button', name=re.compile('^(Open|Close) navigation$'))
            expect(toggle).to_be_visible()
            nav = page.get_by_role('navigation', name='Main navigation', exact=True)
            expect(nav).not_to_be_visible()
            toggle.click()
            expect(toggle).to_have_attribute('aria-expanded', 'true')
            expect(nav).to_be_visible()
            page.keyboard.press('Escape')
            expect(nav).not_to_be_visible()
            expect(toggle).to_be_focused()
            toggle.click()
            nav.get_by_role('link', name='Field guide', exact=True).click()
            page.wait_for_url(urljoin(base_url, 'docs/'))
            page.get_by_role('link', name='Your first project', exact=False).first.click()
            guide_toggle = page.get_by_role('button', name='Browse the field guide', exact=False)
            expect(guide_toggle).to_be_visible()
            guide_nav = page.get_by_role('navigation', name='Guide navigation', exact=True)
            expect(guide_nav).not_to_be_visible()
            guide_toggle.click()
            expect(guide_nav).to_be_visible()
            page.keyboard.press('Escape')
            expect(guide_nav).not_to_be_visible()
            expect(guide_toggle).to_be_focused()
            guide_toggle.click()
            guide_nav.get_by_role('link', name='Brushes', exact=False).click()
            page.wait_for_url(urljoin(base_url, 'docs/brushes-and-dynamics/'))
            expect(page.get_by_role('heading', level=1)).to_contain_text('Brush')

        def guide_sections_and_neighbors():
            home()
            page.goto(urljoin(base_url, 'docs/brushes-and-dynamics/'), wait_until='networkidle')
            toc = page.get_by_role('complementary', name='On this page')
            section = toc.get_by_role('link').nth(1)
            target = section.get_attribute('href')
            section.click()
            assert urlsplit(page.url).fragment == target.removeprefix('#')
            expect(page.locator(target)).to_be_visible()
            neighbors = page.get_by_role('navigation', name='Adjacent guides')
            next_link = neighbors.get_by_role('link').last
            destination = next_link.get_attribute('href')
            next_link.click()
            page.wait_for_url(urljoin(base_url, destination))
            expect(page.get_by_role('heading', level=1)).to_be_visible()

        check('Local search: shortcut, query, arrow/Enter navigation, Escape and focus', search_keyboard)
        check('Local search: no results, literal punctuation, recovery and close', search_empty_and_punctuation)
        check('Local search: immediate Enter uses the current query, including after zero results', search_immediate_enter)
        check('Workflow tabs: arrows, wrap, Home/End, changed artwork and real guide navigation', workflow_tabs)
        check('Screenshot preview: keyboard open, image/alt, modal focus containment and focus return', image_dialog_keyboard)
        check('Mobile main and guide menus: open, Escape, focus return and navigation', mobile_navigation)
        check('Guide table of contents and next-guide navigation', guide_sections_and_neighbors)

        for width in (390, 1440):
            for relative in paths:
                def layout(relative=relative, width=width):
                    page.set_viewport_size({'width': width, 'height': 844 if width == 390 else 1000})
                    page.goto(urljoin(base_url, relative), wait_until='networkidle')
                    expect(page.get_by_role('heading', level=1)).to_be_visible()
                    dimensions = page.evaluate('({width:innerWidth, scroll:document.documentElement.scrollWidth})')
                    assert dimensions['scroll'] <= dimensions['width'] + 1, f'Horizontal overflow: {dimensions}'
                    # Load actual content images to catch bad paths hidden below the fold.
                    for image in page.locator('img[src]').all():
                        if not image.is_visible():
                            continue
                        image.scroll_into_view_if_needed()
                        image.evaluate('async e => { if (!e.complete) await e.decode(); }')
                        assert image.evaluate('e => e.naturalWidth > 0'), f'Broken image: {image.get_attribute("src")}'
                check(f'{width}px layout and images: {relative}', layout)
        home(1440)
        page.screenshot(path=str(report_dir / 'desktop-home.png'), full_page=True)
        home(390)
        page.screenshot(path=str(report_dir / 'mobile-home.png'), full_page=True)
        browser.close()
    if console_errors:
        problems.append(f'Browser errors: {console_errors}')
    if requests:
        problems.append(f'Failed local resources: {requests}')
    report = {'passed': not problems, 'checks': checks, 'errors': problems,
              'consoleErrors': console_errors, 'failedResponses': requests,
              'browser': 'Installed Google Chrome, isolated headless profile', 'baseUrl': base_url}
    (report_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8765/ToonPro/')
    parser.add_argument('--output', type=Path, default=Path('build/website-preview/ToonPro'))
    parser.add_argument('--report-dir', type=Path, default=Path('build/release-075-website-browser'))
    parser.add_argument('--chrome', default='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')
    args = parser.parse_args()
    raise SystemExit(test_site(args.url, args.output, args.report_dir, args.chrome))
