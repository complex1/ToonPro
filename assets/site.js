(() => {
'use strict';
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const base = document.body.dataset.base;
let toastTimer;
function toast(message) { const el = $('#toast'); el.textContent = message; el.classList.add('visible'); clearTimeout(toastTimer); toastTimer = setTimeout(() => el.classList.remove('visible'), 2300); }
const menu = $('.menu-toggle');
menu?.addEventListener('click', () => { const open = menu.getAttribute('aria-expanded') !== 'true'; menu.setAttribute('aria-expanded', String(open)); menu.setAttribute('aria-label', open ? 'Close navigation' : 'Open navigation'); $('#navigation').classList.toggle('open', open); });
document.addEventListener('keydown', e => { if (e.key === 'Escape' && menu?.getAttribute('aria-expanded') === 'true') { menu.click(); menu.focus(); } });
// Shared 600 ms delayed help for icon controls.
let tooltipTimer;
const tooltip = $('#tooltip');
function hideTooltip() { clearTimeout(tooltipTimer); tooltip.hidden = true; }
$$('[data-tooltip]').forEach(el => {
  el.addEventListener('pointerenter', () => { hideTooltip(); tooltipTimer = setTimeout(() => { if (document.querySelector('dialog[open]')) return; tooltip.textContent = el.dataset.tooltip; tooltip.hidden = false; const box = el.getBoundingClientRect(); const width = tooltip.offsetWidth; tooltip.style.left = Math.max(8, Math.min(innerWidth - width - 8, box.left + box.width / 2 - width / 2)) + 'px'; tooltip.style.top = Math.min(innerHeight - tooltip.offsetHeight - 8, box.bottom + 9) + 'px'; }, 600); });
  el.addEventListener('pointerleave', hideTooltip); el.addEventListener('pointerdown', hideTooltip); el.addEventListener('blur', hideTooltip);
});
addEventListener('scroll', hideTooltip, {passive: true});
// ARIA tabs support direct clicks, arrows, Home and End.
$$('[role=tablist]').forEach(list => {
 const tabs = $$('[role=tab]', list);
 const activate = tab => { tabs.forEach(t => { const selected = t === tab; t.setAttribute('aria-selected', String(selected)); t.tabIndex = selected ? 0 : -1; const panel = document.getElementById(t.getAttribute('aria-controls')); if (panel) panel.hidden = !selected; }); };
 tabs.forEach((tab, i) => { tab.addEventListener('click', () => activate(tab)); tab.addEventListener('keydown', e => { let next; if (e.key === 'ArrowRight' || e.key === 'ArrowDown') next = tabs[(i + 1) % tabs.length]; if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') next = tabs[(i + tabs.length - 1) % tabs.length]; if (e.key === 'Home') next = tabs[0]; if (e.key === 'End') next = tabs[tabs.length - 1]; if (next) { e.preventDefault(); activate(next); next.focus(); } }); });
});
// Local documentation search. All result text is inserted as text, never HTML.
const searchDialog = $('#search-dialog'), searchInput = $('#global-search'), results = $('#search-results');
let index = null, indexPromise, searchTrigger, selectedResult = -1, pendingEnter = false;
async function getIndex() { if (!indexPromise) indexPromise = fetch(base + 'search-index.json').then(r => { if (!r.ok) throw new Error('Search unavailable'); return r.json(); }).then(data => { index = data; return data; }).catch(e => { indexPromise = null; throw e; }); return indexPromise; }
function renderSearch() {
 if (!index) return;
 const words = searchInput.value.trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
 const matches = index.map(item => { const title = item.title.toLocaleLowerCase(); const text = (item.title + ' ' + item.text + ' ' + item.category).toLocaleLowerCase(); return {item, score: words.reduce((n,w) => n + (title.includes(w) ? 4 : text.includes(w) ? 1 : -100), 0)}; }).filter(x => x.score >= words.length).sort((a,b) => b.score - a.score).slice(0,30);
 results.replaceChildren(); selectedResult = -1;
 $('#search-count').textContent = words.length ? `${matches.length}${matches.length === 30 ? '+' : ''} results for “${searchInput.value.trim()}”` : 'Start with a chapter, or type to search features and workflows.';
 if (!matches.length) { const p = document.createElement('p'); p.className = 'empty-state'; p.textContent = 'No results. Try a tool name such as “brush” or “fill”.'; results.append(p); }
 matches.forEach(({item}) => { const a = document.createElement('a'); a.className = 'search-result'; a.href = item.url; const category = document.createElement('span'); category.textContent = item.category; const strong = document.createElement('strong'); strong.textContent = item.title; const p = document.createElement('p'); p.textContent = item.text.slice(0,150); a.append(category,strong,p); results.append(a); });
 if (pendingEnter) { pendingEnter = false; $('.search-result', results)?.click(); }
}
async function openSearch(trigger) { searchTrigger = trigger || document.activeElement; hideTooltip(); if (!searchDialog.open) searchDialog.showModal(); searchInput.value = ''; pendingEnter = false; results.replaceChildren(); $('#search-count').textContent = 'Loading the local search index…'; searchInput.focus(); try { await getIndex(); if (searchDialog.open) renderSearch(); } catch { $('#search-count').textContent = 'Search could not load. Open Documentation to browse all chapters.'; } }
function closeSearch() { pendingEnter = false; searchDialog.close(); searchTrigger?.focus(); }
$$('[data-search]').forEach(b => b.addEventListener('click', () => openSearch(b)));
$('[data-close-search]').addEventListener('click', closeSearch);
searchDialog.addEventListener('cancel', e => { e.preventDefault(); closeSearch(); });
searchDialog.addEventListener('keydown', e => { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); closeSearch(); } });
searchInput.addEventListener('input', () => { pendingEnter = false; renderSearch(); });
searchInput.addEventListener('keydown', e => {
 const links = $$('.search-result',results); if (!links.length) { if (e.key === 'Enter' && !index) { e.preventDefault(); pendingEnter = true; } return; }
 if (e.key === 'ArrowDown' || e.key === 'ArrowUp') { e.preventDefault(); selectedResult = selectedResult < 0 ? (e.key === 'ArrowDown' ? 0 : links.length - 1) : (selectedResult + (e.key === 'ArrowDown' ? 1 : -1) + links.length) % links.length; links.forEach((a,i) => a.classList.toggle('selected',i === selectedResult)); links[selectedResult].scrollIntoView({block:'nearest'}); }
 if (e.key === 'Enter') { e.preventDefault(); links[Math.max(0, selectedResult)].click(); }
});
document.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'k') { e.preventDefault(); openSearch(document.activeElement); } });
results.addEventListener('click', e => { if (e.target.closest('a')) closeSearch(); });
// Actual application captures, with native dialog focus containment.
const imageDialog = $('#image-dialog'); let imageTrigger;
$$('[data-zoom]').forEach(b => b.addEventListener('click', () => { hideTooltip(); imageTrigger = b; $('#full-image').src = b.dataset.zoom; $('#open-original').href = b.dataset.zoom; $('#full-image').alt = b.dataset.caption; $('#image-caption').textContent = b.dataset.caption; imageDialog.showModal(); }));
function closeImage() { imageDialog.close(); imageTrigger?.focus(); }
$('[data-close-image]').addEventListener('click', closeImage);
imageDialog.addEventListener('cancel', e => { e.preventDefault(); closeImage(); });
[searchDialog,imageDialog].forEach(d => d.addEventListener('click', e => { if (e.target !== d) return; const r = d.getBoundingClientRect(); if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) { if (d === searchDialog) closeSearch(); else closeImage(); } }));
// Saved features are optional local preferences. Restricted storage is supported.
let saved = new Set();
try { const value = JSON.parse(localStorage.getItem('toonpro.saved-features.v1') || '[]'); if (Array.isArray(value)) saved = new Set(value.filter(x => typeof x === 'string')); } catch {}
function savePreference() { try { localStorage.setItem('toonpro.saved-features.v1',JSON.stringify([...saved])); return true; } catch { return false; } }
function updateSaved() { $$('[data-save]').forEach(b => { const active = saved.has(b.dataset.save); b.setAttribute('aria-pressed',String(active)); b.textContent = active ? '★' : '☆'; b.setAttribute('aria-label',(active ? 'Unsave ' : 'Save ') + $('.feature-main strong',b.closest('.feature-row')).textContent); }); $$('[data-saved-count]').forEach(el => el.textContent = saved.size); }
$$('[data-save]').forEach(b => b.addEventListener('click', () => { const active = saved.has(b.dataset.save); if (active) saved.delete(b.dataset.save); else saved.add(b.dataset.save); const persisted = savePreference(); updateSaved(); applyFilters(); toast(persisted ? (active ? 'Removed from saved features.' : 'Feature saved on this browser.') : 'Feature saved for this visit; browser storage is unavailable.'); }));
updateSaved();
function applyFilters() {
 $$('[data-filter-root]').forEach(root => {
  const query = ($('[data-filter-search]',root)?.value || '').trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
  const category = $('.filter-chip.active[data-category-filter]',root)?.dataset.categoryFilter || 'all';
  const onlySaved = $('[data-saved-filter]',root)?.getAttribute('aria-pressed') === 'true';
  const items = $$('[data-filter-item]',root); let count = 0;
  items.forEach(item => { const text = (item.dataset.text + ' ' + item.dataset.category).toLocaleLowerCase(); const show = (category === 'all' || item.dataset.category === category) && query.every(word => text.includes(word)) && (!onlySaved || saved.has(item.dataset.feature)); item.hidden = !show; if (show) count++; });
  $('[data-filter-count]',root).textContent = `${count} ${root.hasAttribute('data-feature-library') ? 'features' : 'chapters'}`;
  $('[data-empty]',root).hidden = count !== 0;
 });
}
$$('[data-filter-root]').forEach(root => { $('[data-filter-search]',root)?.addEventListener('input',applyFilters); $$('[data-category-filter]',root).forEach(b => b.addEventListener('click', () => { $$('[data-category-filter]',root).forEach(chip => { const active = chip === b; chip.classList.toggle('active',active); chip.setAttribute('aria-pressed',String(active)); }); applyFilters(); })); $('[data-saved-filter]',root)?.addEventListener('click',e => { const b = e.currentTarget; b.setAttribute('aria-pressed',String(b.getAttribute('aria-pressed') !== 'true')); applyFilters(); }); $('[data-expand-all]',root)?.addEventListener('click',e => { const items = $$('[data-filter-item]:not([hidden]) details',root); const open = items.some(x => !x.open); items.forEach(x => x.open = open); e.currentTarget.textContent = open ? 'Collapse results −' : 'Expand results +'; }); });
function openHash() { let id; try { id = decodeURIComponent(location.hash.slice(1)); } catch { return; } const item = document.getElementById(id); if (!item?.matches('[data-feature]')) return; const root = item.closest('[data-filter-root]'); $('[data-filter-search]',root).value = ''; $('[data-category-filter="all"]',root).click(); const savedFilter = $('[data-saved-filter]',root); if (savedFilter.getAttribute('aria-pressed') === 'true') savedFilter.click(); $('details',item).open = true; requestAnimationFrame(() => item.scrollIntoView({block:'start'})); }
addEventListener('hashchange',openHash); openHash();
async function copyText(text, message) { try { await navigator.clipboard.writeText(text); toast(message); return true; } catch { toast('Copy unavailable. Select the text and use your keyboard to copy.'); return false; } }
$$('[data-copy]').forEach(b => b.addEventListener('click', async () => { if (await copyText($('code',b.closest('.code-block')).textContent,'Commands copied.')) { b.textContent = 'Copied ✓'; setTimeout(() => b.textContent = 'Copy',1800); } }));
$$('[data-feature-link]').forEach(b => b.addEventListener('click', () => copyText(location.origin + base + 'features/#' + b.dataset.featureLink,'Feature link copied.')));
// Documentation reading progress and location-aware section navigation.
const article = $('.doc-article');
if (article) {
 const progress = $('.reading-progress'), sections = $$('.article-section',article), links = $$('.toc nav a');
 const update = () => { const top = article.getBoundingClientRect().top + scrollY; const total = article.offsetHeight - innerHeight + 100; progress.style.width = Math.min(100,Math.max(0,(scrollY - top + 100) / Math.max(1,total) * 100)) + '%'; let active = sections[0]?.id; sections.forEach(s => { if (s.getBoundingClientRect().top < 180) active = s.id; }); links.forEach(a => a.classList.toggle('active',a.hash === '#'+active)); };
 addEventListener('scroll',update,{passive:true}); update();
}
// Browser-only ink playground. Keep its engine separate from native app claims.
const canvas = $('#ink-canvas');
if (canvas) {
 const ctx = canvas.getContext('2d'); let color = '#292735', size = 8, drawing = false, last, strokes = [], current;
 const pos = e => { const r = canvas.getBoundingClientRect(); return {x:(e.clientX-r.left)/r.width,y:(e.clientY-r.top)/r.height,pressure:e.pointerType === 'pen' ? Math.max(.15,e.pressure) : .6}; };
 const line = (a,b,stroke) => { const r = canvas.getBoundingClientRect(); ctx.strokeStyle = stroke.color; ctx.fillStyle = stroke.color; ctx.lineWidth = stroke.size * (.45 + b.pressure); ctx.lineCap = 'round'; ctx.lineJoin = 'round'; ctx.beginPath(); ctx.moveTo(a.x*r.width,a.y*r.height); ctx.lineTo(b.x*r.width,b.y*r.height); ctx.stroke(); };
 const paint = () => { const r = canvas.getBoundingClientRect(); ctx.clearRect(0,0,r.width,r.height); strokes.forEach(stroke => { if (stroke.points.length === 1) { const p = stroke.points[0]; ctx.fillStyle = stroke.color; ctx.beginPath(); ctx.arc(p.x*r.width,p.y*r.height,stroke.size/2,0,Math.PI*2); ctx.fill(); } else stroke.points.slice(1).forEach((p,i) => line(stroke.points[i],p,stroke)); }); };
 const resize = () => { const r = canvas.getBoundingClientRect(), dpr = Math.min(devicePixelRatio || 1,3); canvas.width = Math.round(r.width*dpr); canvas.height = Math.round(r.height*dpr); ctx.setTransform(dpr,0,0,dpr,0,0); paint(); };
 new ResizeObserver(resize).observe(canvas);
 canvas.addEventListener('pointerdown',e => { if (e.button !== 0) return; e.preventDefault(); canvas.setPointerCapture(e.pointerId); drawing = true; last = pos(e); current = {color,size,points:[last]}; strokes.push(current); paint(); });
 canvas.addEventListener('pointermove',e => { if (!drawing) return; e.preventDefault(); for (const event of (e.getCoalescedEvents?.().length ? e.getCoalescedEvents() : [e])) { const next = pos(event); current.points.push(next); line(last,next,current); last = next; } });
 const stop = () => { drawing = false; current = null; }; canvas.addEventListener('pointerup',stop); canvas.addEventListener('pointercancel',stop); canvas.addEventListener('lostpointercapture',stop);
 $$('[data-ink]').forEach(b => b.addEventListener('click', () => { color = b.dataset.ink; $$('[data-ink]').forEach(x => x.setAttribute('aria-pressed',String(x === b))); }));
 const changeSize = e => { const value = Number(e.target.value); if (!Number.isFinite(value) || value < 1 || value > 28) return; size = value; $('#ink-size').value = value; $('#ink-number').value = value; };
 $('#ink-size').addEventListener('input',changeSize); $('#ink-number').addEventListener('input',changeSize); $('#ink-number').addEventListener('change', () => { $('#ink-number').value = size; });
 $('#clear-ink').addEventListener('click', () => { strokes = []; paint(); toast('A fresh little canvas.'); });
 $('#save-ink').addEventListener('click', () => { const output = document.createElement('canvas'); output.width = canvas.width; output.height = canvas.height; const c = output.getContext('2d'); c.fillStyle = '#f8f5ee'; c.fillRect(0,0,output.width,output.height); c.drawImage(canvas,0,0); output.toBlob(blob => { if (!blob) return; const url = URL.createObjectURL(blob); const a = document.createElement('a'); a.href = url; a.download = 'toonpro-browser-doodle.png'; a.click(); setTimeout(() => URL.revokeObjectURL(url),1000); }); });
}
})();
