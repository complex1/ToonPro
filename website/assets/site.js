/* Toon Pro's static site: navigation, local search and real screenshot previews. */
(() => {
  'use strict';
  const base = document.body.dataset.base || '/';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const menu = $('.menu-toggle'), nav = $('#primary-nav');
  const sidebarToggle = $('.docs-mobile-toggle'), sidebar = $('#docs-sidebar');
  function toggleNavigation(button, region, force) {
    if (!button || !region) return;
    const open = force ?? button.getAttribute('aria-expanded') !== 'true';
    button.setAttribute('aria-expanded', String(open));
    region.dataset.open = String(open);
    if (button === menu) button.setAttribute('aria-label', open ? 'Close navigation' : 'Open navigation');
  }
  menu?.addEventListener('click', () => toggleNavigation(menu, nav));
  sidebarToggle?.addEventListener('click', () => toggleNavigation(sidebarToggle, sidebar));
  $$('a', nav).forEach(link => link.addEventListener('click', () => toggleNavigation(menu, nav, false)));
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') {
      if (menu?.getAttribute('aria-expanded') === 'true') { toggleNavigation(menu, nav, false); menu.focus(); }
      if (sidebarToggle?.getAttribute('aria-expanded') === 'true') { toggleNavigation(sidebarToggle, sidebar, false); sidebarToggle.focus(); }
    }
  });

  const workflow = {
    draw: {number:'01 / DRAW',title:'Find your line.',description:'Sketch with raster brushes or keep brush strokes editable on vector layers. Shape the mark with pressure, textures, stamps and dynamics.',points:['Brush presets, tags and size shortcuts','Connected brushes and repeating patterns','Perspective guides and symmetry'],guide:'brushes-and-dynamics',link:'Explore drawing tools ↗',image:'brushes-and-dynamics',alt:'Actual Toon Pro brush controls and dynamics preview',caption:'Real brush controls · sample settings'},
    color: {number:'02 / DEFINE',title:'Build a consistent cast.',description:'Keep character references and approved colors close. Organize model sheets, create lighting variants and preview recoloring across selected drawings.',points:['Turnarounds, expressions and costumes','Skin, hair, costume and shadow colors','Day, night, sunset and interior variants'],guide:'references-and-characters',link:'Explore character workflows ↗',image:'character-colors',alt:'Actual character color model panel with grouped palette colors',caption:'Real character palette controls · sample character'},
    animate: {number:'03 / ANIMATE',title:'Give the drawing a rhythm.',description:'Create a new drawing, set its exposure, then move to the next pose. Reuse cels or add property keys for motion that belongs between your drawings.',points:['Frame-by-frame drawing tracks','Transform, path and camera keys','Reusable Symbols and timed poses'],guide:'frame-by-frame-animation',link:'Explore the timeline ↗',image:'frame-by-frame-animation',alt:'Actual frame-by-frame animation timeline and drawing controls',caption:'Real animation timeline · sample drawing track'},
    finish: {number:'04 / FINISH',title:'Finish the moment.',description:'Review the drawing, check cel colors and prepare the shot for output. Keep the editable source while exporting images or an animated result.',points:['Preview repairs before committing','Shot audio, camera and effects','Image, production-sheet and video output'],guide:'audio-movie-and-export',link:'Explore output & export ↗',image:'cel-paint-and-light-table',alt:'Actual Toon Pro Cel Paint Check finishing controls',caption:'Real Cel Paint Check controls · a finishing workflow in the public preview'}
  };
  function selectWorkflow(button) {
    const config = workflow[button.dataset.workflow];
    if (!config) return;
    $$('[data-workflow]').forEach(tab => { const active = tab === button; tab.setAttribute('aria-selected', String(active)); tab.tabIndex = active ? 0 : -1; });
    $('#workflow-panel').setAttribute('aria-labelledby', button.id);
    $('#workflow-number').textContent = config.number;
    $('#workflow-title').textContent = config.title;
    $('#workflow-description').textContent = config.description;
    $('#workflow-points').replaceChildren(...config.points.map(text => { const li=document.createElement('li');li.textContent=text;return li; }));
    $('#workflow-guide').href = `${base}docs/${config.guide}/`;
    $('#workflow-guide').textContent = config.link;
    $('#workflow-image').src = `${base}assets/screenshots/${config.image}.webp`;
    $('#workflow-image').alt = config.alt;
    $('#workflow-image').setAttribute('aria-label', `Enlarge: ${config.alt}`);
    $('#workflow-caption').textContent = config.caption;
  }
  const tabs = $$('[data-workflow]');
  tabs.forEach((tab,index) => {
    tab.addEventListener('click', () => selectWorkflow(tab));
    tab.addEventListener('keydown', event => {
      let next;
      if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
      if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length;
      if (event.key === 'Home') next = 0;
      if (event.key === 'End') next = tabs.length - 1;
      if (next !== undefined) { event.preventDefault(); tabs[next].focus(); selectWorkflow(tabs[next]); }
    });
  });

  const searchDialog = $('#search-dialog'), query = $('#doc-search'), resultArea = $('#search-results');
  let indexPromise, selected = -1, results = [], searchTrigger;
  const loadIndex = () => indexPromise ||= fetch(`${base}search-index.json`).then(response => {
    if (!response.ok) throw new Error('Search index unavailable'); return response.json();
  }).catch(error => { indexPromise = null; throw error; });
  function status(text) { const p=document.createElement('p');p.className='search-status';p.textContent=text;resultArea.replaceChildren(p);$('#search-announcement').textContent=text; }
  function highlightSelection(next) {
    selected = Math.max(0, Math.min(next, results.length - 1));
    results.forEach((link,i) => link.classList.toggle('selected', i === selected));
    results[selected]?.scrollIntoView({block:'nearest'});
  }
  let searchRevision = 0, renderedQuery = null;
  async function search() {
    const revision=++searchRevision;
    const raw=query.value.trim().slice(0,160).toLowerCase();
    const terms=raw.split(/\s+/).filter(Boolean);
    try {
      const index=await loadIndex(); if (revision!==searchRevision) return;
      const matches=index.map((item,position) => {
        const title=item.title.toLowerCase(), description=item.description.toLowerCase(), text=item.text.toLowerCase();
        const score=terms.length ? terms.reduce((sum,term) => sum+(title.includes(term)?12:description.includes(term)?5:text.includes(term)?1:-1000),0) : (item.url.includes('#')?-1000:100-position);
        return {item,score};
      }).filter(match=>match.score>=0).sort((a,b)=>b.score-a.score).slice(0,12);
      results=[];selected=-1;renderedQuery=raw;resultArea.replaceChildren();
      if (!matches.length) { status(`No results for “${query.value.trim()}”. Try a tool name, such as brush, mask or timeline.`); return; }
      for (const {item} of matches) {
        const link=document.createElement('a');link.className='search-result';link.href=item.url;
        const category=document.createElement('small');category.textContent=item.category;
        const title=document.createElement('strong');title.textContent=item.title;
        const description=document.createElement('p');
        let snippet=item.description;
        if (terms.length && !terms.some(term=>(item.title+' '+item.description).toLowerCase().includes(term))) {
          const start=Math.max(0,item.text.toLowerCase().indexOf(terms[0])-50);
          snippet=(start?'…':'')+item.text.slice(start,start+190)+'…';
        }
        description.textContent=snippet;link.append(category,title,description);resultArea.append(link);results.push(link);
      }
      $('#search-announcement').textContent=`${results.length} results shown.`;
    } catch { results=[];renderedQuery=null;status('Search is temporarily unavailable. Close search and browse the field guide by topic.'); }
  }
  function openSearch(trigger) {
    if (searchDialog.open) { query.focus(); return; }
    searchTrigger=trigger || document.activeElement;searchDialog.showModal();document.body.classList.add('no-scroll');query.focus();search();
  }
  function closeSearch() { searchDialog.close();document.body.classList.remove('no-scroll');searchTrigger?.focus(); }
  $$('[data-search]').forEach(button=>button.addEventListener('click',()=>openSearch(button)));
  $('[data-close-search]')?.addEventListener('click',closeSearch);
  searchDialog?.addEventListener('cancel',event=>{event.preventDefault();closeSearch();});
  searchDialog?.addEventListener('click',event=>{if(event.target===searchDialog){const box=searchDialog.getBoundingClientRect();if(event.clientX<box.left||event.clientX>box.right||event.clientY<box.top||event.clientY>box.bottom)closeSearch();}});
  let debounce;
  query?.addEventListener('input',()=>{clearTimeout(debounce);++searchRevision;results=[];selected=-1;renderedQuery=null;status('Searching…');debounce=setTimeout(search,100);});
  searchDialog?.addEventListener('keydown',async event=>{
    if(event.key==='Escape'){event.preventDefault();closeSearch();return;}
    if(event.key==='ArrowDown'||event.key==='ArrowUp'){event.preventDefault();highlightSelection(selected+(event.key==='ArrowDown'?1:-1));query.focus();}
    if(event.key==='Enter'&&document.activeElement===query){
      event.preventDefault();clearTimeout(debounce);
      const requested=query.value, normalized=requested.trim().slice(0,160).toLowerCase();
      const choice=renderedQuery===normalized?Math.max(0,selected):0;
      await search();
      if(searchDialog.open&&query.value===requested&&results.length)location.href=results[Math.min(choice,results.length-1)].href;
    }
  });
  document.addEventListener('keydown',event=>{
    const typing=event.target instanceof HTMLElement && (event.target.matches('input,textarea,select')||event.target.isContentEditable);
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='k'||event.key==='/'&&!typing){event.preventDefault();openSearch();}
  });

  const imageDialog=$('#image-dialog');let imageTrigger;
  function openImage(img,trigger=img) {
    imageTrigger=trigger;$('#enlarged-image').src=img.currentSrc||img.src;$('#enlarged-image').alt=img.alt;
    $('#image-caption').textContent=img.closest('figure')?.querySelector('figcaption')?.textContent||img.alt;
    imageDialog.showModal();document.body.classList.add('no-scroll');
  }
  function closeImage(){imageDialog.close();document.body.classList.remove('no-scroll');imageTrigger?.focus();}
  $$('[data-zoom]').forEach(img=>{img.tabIndex=0;img.setAttribute('role','button');img.setAttribute('aria-label',`Enlarge: ${img.alt}`);img.addEventListener('click',()=>openImage(img));img.addEventListener('keydown',event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();openImage(img);}});});
  $$('[data-zoom-target]').forEach(button=>button.addEventListener('click',()=>openImage(document.getElementById(button.dataset.zoomTarget),button)));
  $('[data-close-image]')?.addEventListener('click',closeImage);
  imageDialog?.addEventListener('cancel',event=>{event.preventDefault();closeImage();});
  imageDialog?.addEventListener('click',event=>{if(event.target===imageDialog)closeImage();});
  $$('[data-print]').forEach(button=>button.addEventListener('click',()=>window.print()));

  if ('IntersectionObserver' in window && $('.doc-prose')) {
    const observer=new IntersectionObserver(entries=>{for(const entry of entries){if(entry.isIntersecting){$$('.doc-toc a').forEach(link=>link.classList.toggle('current',link.hash==='#'+entry.target.id));}}},{rootMargin:'-100px 0px -65% 0px'});
    $$('.doc-prose h2[id],.doc-prose h3[id]').forEach(heading=>observer.observe(heading));
  }
})();
