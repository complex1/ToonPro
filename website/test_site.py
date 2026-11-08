#!/usr/bin/env python3
"""Exercise built Toon Pro pages in an isolated Chrome/Chromium browser profile."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from urllib.parse import urljoin, urlsplit
from playwright.sync_api import sync_playwright, expect


def test_site(base_url, output, report_dir, chrome=None):
    report_dir.mkdir(parents=True, exist_ok=True)
    checks, problems, console, requests = [], [], [], []
    observations = []
    catalog = json.loads((Path(__file__).resolve().parent/'data/manual.json').read_text())
    feature_count = len(catalog['features'])
    origin = urlsplit(base_url).netloc
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True, **({'executable_path':chrome} if chrome else {}))
        context = browser.new_context(viewport={'width':1440,'height':1000}, reduced_motion='reduce', accept_downloads=True)
        context.grant_permissions(['clipboard-read','clipboard-write'])
        page = context.new_page()
        page.set_default_timeout(7000)
        page.on('pageerror', lambda error: console.append(str(error)))
        page.on('console', lambda message: console.append(message.text) if message.type == 'error' else None)
        page.on('response', lambda response: requests.append(f'{response.status}: {response.url}')
                if urlsplit(response.url).netloc == origin and response.status >= 400 else None)
        page.on('requestfailed', lambda request: requests.append(f'{request.failure}: {request.url}')
                if urlsplit(request.url).netloc == origin else None)

        def go(route=''):
            page.goto(urljoin(base_url, route), wait_until='networkidle')

        def check(name, action):
            try:
                action()
                checks.append({'name':name,'passed':True})
                print('PASS: '+name, flush=True)
            except Exception as error:
                problems.append(name+': '+str(error))
                checks.append({'name':name,'passed':False,'error':str(error)})
                print('FAIL: '+name+': '+str(error), flush=True)
                page.screenshot(path=str(report_dir/f'failure-{len(problems)}.png'), full_page=True)

        def search_keyboard():
            go()
            trigger = page.get_by_role('button', name='Search documentation', exact=True)
            trigger.focus()
            page.keyboard.press('Control+k')
            expect(page.locator('#search-dialog')).to_be_visible()
            field = page.locator('#global-search')
            expect(field).to_be_focused()
            field.fill('puppet warp')
            links = page.locator('.search-result')
            expect(links.first).to_be_visible()
            destination = links.first.get_attribute('href')
            page.keyboard.press('ArrowDown')
            page.keyboard.press('Enter')
            page.wait_for_url(urljoin(base_url,destination))
            expect(page.get_by_role('heading',level=1)).to_be_visible()
            trigger = page.get_by_role('button', name='Search documentation', exact=True)
            trigger.click()
            page.locator('#global-search').fill('<img src=x onerror="window.injected=true">')
            expect(page.locator('#search-count')).to_contain_text('0 results')
            expect(page.locator('#search-results img')).to_have_count(0)
            assert not page.evaluate('Boolean(window.injected)')
            page.keyboard.press('ArrowDown')
            page.keyboard.press('Enter')
            expect(page.locator('#search-dialog')).to_be_visible()
            page.keyboard.press('Escape')
            expect(trigger).to_be_focused()

        def immediate_search():
            # A first search must survive Enter before the local index finishes loading.
            fresh = context.new_page()
            fresh.add_init_script("""const nativeFetch=window.fetch;window.fetch=(...args)=>String(args[0]).includes('search-index.json')?new Promise(resolve=>setTimeout(resolve,350)).then(()=>nativeFetch(...args)):nativeFetch(...args);""")
            try:
                fresh.goto(base_url,wait_until='networkidle')
                fresh.get_by_role('button',name='Search documentation',exact=True).click()
                fresh.locator('#global-search').fill('puppet warp')
                fresh.keyboard.press('Enter')
                fresh.wait_for_url(lambda url:'/docs/' in str(url) or '/features/' in str(url))
                expect(fresh.get_by_role('heading',level=1)).to_be_visible()
            finally: fresh.close()

        def tabs():
            for route,label in [('', 'Explore the workspace'),('setup/','Build platform')]:
                go(route)
                tabs = page.get_by_role('tablist',name=label).get_by_role('tab')
                tabs.first.focus()
                page.keyboard.press('ArrowRight')
                expect(tabs.nth(1)).to_have_attribute('aria-selected','true')
                expect(tabs.nth(1)).to_be_focused()
                expect(page.locator('#'+tabs.nth(1).get_attribute('aria-controls'))).to_be_visible()
                page.keyboard.press('End')
                expect(tabs.last).to_have_attribute('aria-selected','true')
                page.keyboard.press('Home')
                expect(tabs.first).to_have_attribute('aria-selected','true')

        def feature_library():
            go('features/')
            page.evaluate('localStorage.clear()')
            page.reload(wait_until='networkidle')
            expect(page.locator('[data-feature]')).to_have_count(feature_count)
            expect(page.locator('[data-filter-count]')).to_have_text(f'{feature_count} features')
            field = page.locator('[data-filter-search]')
            field.fill('puppet warp')
            rows = page.locator('[data-feature]:visible')
            assert rows.count() > 0
            selected = rows.first
            identifier = selected.get_attribute('id')
            selected.locator('[data-save]').click()
            page.reload(wait_until='networkidle')
            expect(page.locator(f'#{identifier} [data-save]')).to_have_attribute('aria-pressed','true')
            page.locator('[data-saved-filter]').click()
            expect(page.locator('[data-feature]:visible')).to_have_count(1)
            page.locator('[data-filter-search]').fill('a-word-that-has-no-results')
            expect(page.locator('[data-empty]')).to_be_visible()
            go('features/#'+identifier)
            row = page.locator('#'+identifier)
            expect(row).to_be_visible()
            expect(row.locator('details')).to_have_attribute('open','')
            row.locator('[data-feature-link]').click()
            expect(page.locator('#toast')).to_contain_text('Feature link copied')
            assert page.evaluate('navigator.clipboard.readText()') == urljoin(base_url,'features/#'+identifier)
            category = page.locator('[data-category-filter]').nth(1)
            category.click()
            actual = page.locator('[data-feature]:visible').evaluate_all('(rows)=>rows.map(r=>r.dataset.category)')
            assert actual and set(actual) == {category.get_attribute('data-category-filter')}
            page.locator('[data-expand-all]').click()
            assert page.locator('[data-feature]:visible details').evaluate_all('(rows)=>rows.every(r=>r.open)')
            page.locator('[data-expand-all]').click()
            assert page.locator('[data-feature]:visible details').evaluate_all('(rows)=>rows.every(r=>!r.open)')
            page.screenshot(path=str(report_dir/'features-desktop.png'),full_page=False)

        def feature_media():
            # The initial view must show actual app imagery before any disclosure opens.
            for width in (375,768,1440):
                page.set_viewport_size({'width':width,'height':844 if width==375 else 1000})
                go('features/')
                expect(page.locator('[data-feature]')).to_have_count(feature_count)
                visible = page.locator('.feature-intro .app-capture img,.feature-showcase .app-capture img').evaluate_all('''images=>images.some(image=>{
                    const r=image.getBoundingClientRect(),s=getComputedStyle(image);
                    return s.visibility!=='hidden'&&Number(s.opacity)>0&&r.width>100&&r.height>60
                        &&Math.min(r.bottom,innerHeight)-Math.max(r.top,0)>40;
                })''')
                assert visible, f'Features: no meaningful screenshot in the initial {width}px view'
            rows = page.locator('[data-feature]')
            expect(rows.locator('img.feature-thumb')).to_have_count(feature_count)
            failures = rows.evaluate_all('''async rows=>{
                const errors=[];
                for(const row of rows){
                    const thumb=row.querySelector('img.feature-thumb'),image=row.querySelector('.feature-detail .app-capture img');
                    if(!thumb||!image){errors.push(row.id+': missing thumbnail or expanded capture');continue;}
                    for(const shot of [thumb,image]){shot.loading='eager';try{await shot.decode();}catch{errors.push(row.id+': failed capture decode');}}
                    if(!thumb.checkVisibility()||!thumb.getBoundingClientRect().width)errors.push(row.id+': thumbnail hidden');
                    if(new URL(thumb.src).pathname!==new URL(image.src).pathname)errors.push(row.id+': thumbnail/detail capture differs');
                    if(!row.dataset.capture)errors.push(row.id+': no mapped capture identity');
                }return errors;
            }''')
            assert not failures, '\n'.join(failures)
            first = rows.first
            first.locator('img.feature-thumb').click()
            expect(first.locator('details')).to_have_attribute('open','')
            trigger = first.locator('.feature-detail .app-capture [data-zoom]').first
            trigger.click()
            dialog = page.locator('#image-dialog')
            expect(dialog).to_be_visible()
            page.locator('#full-image').evaluate('(image)=>image.decode()')
            expected_image = urljoin(base_url,trigger.get_attribute('data-zoom'))
            assert page.locator('#full-image').get_attribute('src') == trigger.get_attribute('data-zoom')
            original = page.locator('#open-original')
            expect(original).to_be_visible()
            assert urljoin(base_url,original.get_attribute('href')) == expected_image
            expect(original).to_have_attribute('target','_blank')
            page.keyboard.press('Tab')
            assert page.evaluate('Boolean(document.activeElement.closest("#image-dialog"))'), 'Image-dialog focus escaped'
            page.keyboard.press('Escape')
            expect(dialog).to_be_hidden()
            expect(trigger).to_be_focused()
            page.locator('[data-expand-all]').click()
            expect(rows.locator('.feature-detail .app-capture:visible')).to_have_count(feature_count)
            page.locator('[data-filter-search]').fill('puppet warp')
            matched = page.locator('[data-feature]:visible')
            assert matched.count() > 0
            expect(matched.locator('img.feature-thumb')).to_have_count(matched.count())
            expect(matched.locator('.feature-detail .app-capture:visible')).to_have_count(matched.count())
            page.locator('[data-filter-search]').fill('')
            expect(page.locator('[data-feature]:visible')).to_have_count(feature_count)
            expect(page.locator('[data-filter-count]')).to_have_text(f'{feature_count} features')
            observations.append({'featureMedia':{'catalogEntries':feature_count,'thumbnailCount':feature_count,'expandedCaptureCount':feature_count,'initialWidths':[375,768,1440]}})

        def typography_and_contrast():
            audit = '''specs=>{
                const canvas=document.createElement('canvas');canvas.width=canvas.height=1;
                const ctx=canvas.getContext('2d',{willReadFrequently:true});
                const rgba=color=>{ctx.clearRect(0,0,1,1);ctx.fillStyle=color;ctx.fillRect(0,0,1,1);return [...ctx.getImageData(0,0,1,1).data].map(x=>x/255);};
                const over=(top,bottom)=>{const a=top[3]+bottom[3]*(1-top[3]);return a?[...top.slice(0,3).map((v,i)=>(v*top[3]+bottom[i]*bottom[3]*(1-top[3]))/a),a]:[0,0,0,0];};
                const pixel=(element,text)=>{
                    let paint=rgba(getComputedStyle(element).backgroundColor);
                    if(text)paint=over(rgba(getComputedStyle(element).color),paint);
                    for(let node=element;node;node=node.parentElement){
                        paint[3]*=Number(getComputedStyle(node).opacity);
                        if(node.parentElement)paint=over(paint,rgba(getComputedStyle(node.parentElement).backgroundColor));
                    }
                    return over(paint,[1,1,1,1]);
                };
                const luminance=c=>c.slice(0,3).map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4).reduce((n,v,i)=>n+v*[.2126,.7152,.0722][i],0);
                const failures=[],measurements=[];
                for(const spec of specs){
                    const elements=[...document.querySelectorAll(spec.selector)].filter(e=>e.checkVisibility()&&e.textContent.trim());
                    if(spec.required&&!elements.length){failures.push(spec.label+': no visible sample');continue;}
                    const samples=spec.contrast?elements.slice(0,3):elements;
                    let minSize=Infinity,minContrast=Infinity;
                    for(const element of samples){
                        const s=getComputedStyle(element),size=parseFloat(s.fontSize),weight=parseFloat(s.fontWeight)||400;
                        minSize=Math.min(minSize,size);
                        if(size+0.01<spec.min)failures.push(spec.label+': '+size+'px < '+spec.min+'px');
                        if(spec.contrast){
                            const a=luminance(pixel(element,true)),b=luminance(pixel(element,false)),ratio=(Math.max(a,b)+.05)/(Math.min(a,b)+.05);
                            const threshold=size>=24||(size>=18.66&&weight>=700)?3:4.5;
                            minContrast=Math.min(minContrast,ratio);
                            if(ratio+.001<threshold)failures.push(spec.label+': contrast '+ratio.toFixed(2)+' < '+threshold+' ('+s.color+')');
                        }
                    }
                    if(elements.length)measurements.push({label:spec.label,count:elements.length,minPx:minSize,minContrast:Number.isFinite(minContrast)?Number(minContrast.toFixed(2)):null});
                }return {failures,measurements};
            }'''
            for width in (375,768,1440):
                page.set_viewport_size({'width':width,'height':844 if width==375 else 1000})
                for route in ('','docs/local-setup/','features/'):
                    go(route)
                    page.evaluate('document.fonts.ready')
                    if route=='features/': page.locator('[data-expand-all]').click()
                    menu = page.get_by_role('button',name='Open navigation',exact=True)
                    menu_opened = menu.is_visible() and not page.locator('#navigation').is_visible()
                    if menu_opened: menu.click()
                    specs=[{'selector':'.site-header nav a','min':14,'contrast':True,'required':True,'label':'main navigation'}]
                    if route=='docs/local-setup/':
                        specs += [
                            {'selector':'.article-section p,.doc-article .steps li','min':17 if width==1440 else 16,'required':True,'label':'article prose'},
                            {'selector':'.article-section p','min':17 if width==1440 else 16,'contrast':True,'required':True,'label':'article paragraph contrast'},
                            {'selector':'.article-lead','min':17 if width==1440 else 16,'contrast':True,'required':True,'label':'article lead'},
                            {'selector':'.sidebar-group a','min':14,'contrast':True,'required':width==1440,'label':'manual sidebar'},
                            {'selector':'.version-note','min':12,'contrast':True,'required':True,'label':'source version note'},
                            {'selector':'.doc-article figcaption','min':12,'contrast':True,'required':True,'label':'documentation image caption'},
                            {'selector':'.doc-article .eyebrow,.source-links,.source-links a','min':12,'label':'documentation metadata'},
                        ]
                    elif route=='features/':
                        specs += [
                            {'selector':'.feature-main>span:last-child,.feature-main strong,.feature-detail p,.feature-detail li','min':16,'required':True,'label':'feature prose'},
                            {'selector':'.wide-intro,.feature-detail p','min':16,'contrast':True,'required':True,'label':'feature paragraph contrast'},
                            {'selector':'.filter-chip,.filter-search input,.filter-search>span','min':14,'required':True,'label':'feature filters'},
                            {'selector':'.filter-chip.active,.filter-chip:not(.active)','min':14,'contrast':True,'required':True,'label':'feature filter contrast'},
                            {'selector':'.feature-category,.status-badge,.status-key,.local-note','min':12,'contrast':True,'required':True,'label':'feature metadata'},
                            {'selector':'.feature-detail figcaption','min':12,'contrast':True,'required':True,'label':'feature image caption'},
                        ]
                    else:
                        specs += [
                            {'selector':'.hero-copy>p:not(.build-note)','min':16,'contrast':True,'required':True,'label':'landing-page lead'},
                            {'selector':'.build-note,.visual-caption,.section-label,.eyebrow','min':12,'contrast':True,'required':True,'label':'landing metadata'},
                        ]
                    result=page.evaluate(audit,specs)
                    assert not result['failures'], f'{route or "home"} at {width}px: '+'; '.join(result['failures'][:15])
                    observations.append({'readability':{'route':route or 'home','viewport':width,'samples':result['measurements']}})
                    if route=='docs/local-setup/':
                        image=page.locator('.doc-article>.app-capture img').first
                        image.evaluate('(image)=>image.decode()')
                        size=image.evaluate('(image)=>({width:image.getBoundingClientRect().width,native:image.naturalWidth,column:document.querySelector(".doc-article").clientWidth})')
                        assert size['width'] >= min(size['native'],size['column'])*.85, f'Documentation screenshot unnecessarily shrunk at {width}px: {size}'
                    if menu_opened: page.keyboard.press('Escape')
            font_info=page.evaluate(r'''()=>({faces:[...document.fonts].map(f=>({family:f.family,status:f.status})),body:getComputedStyle(document.body).fontFamily,heading:getComputedStyle(document.querySelector('h1')).fontFamily,resources:performance.getEntriesByType('resource').map(r=>r.name).filter(n=>/\.(ttf|woff2?)(\?|$)/.test(n))})''')
            normalize=lambda value: ''.join(c for c in value.lower() if c.isalpha())
            for family,element in [('Space Grotesk','body'),('Bricolage Grotesque','heading')]:
                assert any(normalize(family)==normalize(face['family']) and face['status']=='loaded' for face in font_info['faces']), 'Font not loaded: '+family
                assert normalize(family)==normalize(font_info[element].split(',')[0]), f'{element} does not use '+family
            for filename in ('body-readable.ttf','display-readable.ttf'):
                assert any(urlsplit(resource).netloc==origin and urlsplit(resource).path.endswith('/'+filename) for resource in font_info['resources']), 'Self-hosted font not fetched: '+filename
            observations.append({'loadedTypography':font_info})

        def manual_and_copy():
            go('docs/')
            category = page.locator('[data-category-filter]').nth(1)
            category.click()
            cards = page.locator('.chapter-card:visible')
            assert cards.count() > 0
            assert set(cards.evaluate_all('(cards)=>cards.map(c=>c.dataset.category)')) == {category.get_attribute('data-category-filter')}
            cards.first.click()
            expect(page.get_by_role('heading',level=1)).to_be_visible()
            expect(page.get_by_role('navigation',name='Manual chapters')).to_be_visible()
            go('setup/')
            panel = page.get_by_role('tabpanel').filter(visible=True)
            expected = panel.locator('code').inner_text()
            panel.locator('[data-copy]').click()
            expect(page.locator('#toast')).to_contain_text('Commands copied')
            assert page.evaluate('navigator.clipboard.readText()') == expected

        def screenshots_and_ink():
            go()
            trigger = page.locator('[data-zoom]').first
            trigger.click()
            expect(page.locator('#image-dialog')).to_be_visible()
            page.wait_for_function('document.querySelector("#full-image").complete && document.querySelector("#full-image").naturalWidth>0')
            page.keyboard.press('Escape')
            expect(trigger).to_be_focused()
            canvas = page.locator('#ink-canvas')
            canvas.scroll_into_view_if_needed()
            page.get_by_role('button',name='Lilac ink').click()
            page.locator('#ink-number').fill('18')
            page.locator('#ink-number').dispatch_event('input')
            expect(page.locator('#ink-size')).to_have_value('18')
            canvas.scroll_into_view_if_needed()
            box = canvas.bounding_box()
            page.mouse.move(box['x']+30,box['y']+40)
            page.mouse.down()
            page.mouse.move(box['x']+220,box['y']+120,steps=20)
            page.mouse.up()
            pixels = '(c)=>c.getContext("2d").getImageData(0,0,c.width,c.height).data.some((v,i)=>i%4===3&&v>0)'
            assert canvas.evaluate(pixels)
            with page.expect_download() as download:
                page.locator('#save-ink').click()
            target = report_dir/download.value.suggested_filename
            download.value.save_as(target)
            assert target.read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
            page.locator('#clear-ink').click()
            assert not canvas.evaluate(pixels)

        def all_routes():
            paths = sorted(output.rglob('*.html'))
            for path in paths:
                route = path.relative_to(output).as_posix()
                page.goto(urljoin(base_url,route),wait_until='load')
                expect(page.get_by_role('heading',level=1)).to_be_visible()
                failures = page.evaluate('''async()=>{const images=[...document.images].filter(i=>i.getAttribute('src'));return (await Promise.all(images.map(async i=>{i.loading='eager';try{await i.decode();const capture=i.closest('.app-capture');return i.naturalWidth>0&&(!capture||(!i.hasAttribute('width')||i.naturalWidth===Number(i.getAttribute('width')))&&(!i.hasAttribute('height')||i.naturalHeight===Number(i.getAttribute('height'))))?null:i.src}catch{return i.src}}))).filter(Boolean)}''')
                assert not failures, route+': images failed '+str(failures)
            print(f'  Decoded images across {len(paths)} routes',flush=True)

        def responsive_navigation():
            for width in (375,768,1440):
                page.set_viewport_size({'width':width,'height':844 if width==375 else 1000})
                for route in ('','docs/','docs/local-setup/','features/','setup/','compare/'):
                    go(route)
                    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth+1'), f'{route}: overflow at {width}px'
                go()
                if width == 375:
                    toggle = page.get_by_role('button',name='Open navigation',exact=True)
                    expect(page.locator('#navigation')).to_be_hidden()
                    toggle.click()
                    expect(page.locator('#navigation')).to_be_visible()
                    page.keyboard.press('Escape')
                    expect(toggle).to_be_focused()
                    expect(page.locator('#navigation')).to_be_hidden()
                page.screenshot(path=str(report_dir/f'home-{width}.png'),full_page=True)

        for name, action in [('Search keyboard, empty input safety and focus',search_keyboard),('Immediate first-search Enter',immediate_search),('Workspace and setup keyboard tabs',tabs),('Feature filtering, saves, links and expansion',feature_library),('Feature screenshots, relevant media and keyboard zoom',feature_media),('Manual navigation and exact clipboard commands',manual_and_copy),('Screenshot zoom and real ink/download/clear',screenshots_and_ink),('Every route and decoded content image',all_routes),('Responsive widths and mobile navigation',responsive_navigation),('Readable typography, composite text contrast and loaded fonts',typography_and_contrast)]: check(name,action)
        if console: problems.extend('Browser: '+x for x in console)
        if requests: problems.extend('Request: '+x for x in requests)
        browser.close()
    report = {'passed':not problems,'checks':checks,'errors':problems,'consoleErrors':console,'failedRequests':requests,'observations':observations}
    (report_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-url',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--report-dir',type=Path,required=True)
    parser.add_argument('--chrome')
    args = parser.parse_args()
    raise SystemExit(0 if test_site(args.base_url.rstrip('/')+'/',args.output.resolve(),args.report_dir.resolve(),args.chrome)['passed'] else 1)
