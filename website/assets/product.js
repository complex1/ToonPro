/* Progressive navigation and a deliberately illustrative, user-controlled shot. */
(() => {
  'use strict';
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];

  $$('[data-tab-group]').forEach(group => {
    const tabs = $$('[role="tab"]', group);
    const select = (tab, focus = false) => {
      tabs.forEach(item => {
        const active = item === tab;
        item.setAttribute('aria-selected', String(active));
        item.tabIndex = active ? 0 : -1;
        const panel = document.getElementById(item.getAttribute('aria-controls'));
        if (panel) panel.hidden = !active;
      });
      if (focus) {
        tab.focus({preventScroll: true});
        // Horizontal comparison tabs remain reachable on small screens.
        if (group.scrollWidth > group.clientWidth) {
          const left = tab.offsetLeft - group.offsetLeft;
          if (left < group.scrollLeft) group.scrollLeft = left;
          else if (left + tab.offsetWidth > group.scrollLeft + group.clientWidth)
            group.scrollLeft = left + tab.offsetWidth - group.clientWidth;
        }
      }
    };
    tabs.forEach((tab, index) => {
      tab.addEventListener('click', () => select(tab));
      tab.addEventListener('keydown', event => {
        let next;
        if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
        if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length;
        if (event.key === 'Home') next = 0;
        if (event.key === 'End') next = tabs.length - 1;
        if (next !== undefined) { event.preventDefault(); select(tabs[next], true); }
      });
    });
    select(tabs.find(tab => tab.getAttribute('aria-selected') === 'true') || tabs[0]);
  });

  const plane = $('[data-demo-plane]'), play = $('[data-demo-play]');
  if (plane && play) {
    const frames = $$('[data-demo-frame]'), readout = $('[data-demo-readout]');
    const poses = [[85,180,-8],[170,157,-4],[260,152,-9],[350,130,-17],[448,95,-7],[557,94,10]];
    let frame = 1, timer = null;
    const render = () => {
      const pose = Math.floor((frame - 1) / 2);
      const [x, y, rotation] = poses[pose];
      plane.setAttribute('transform', `translate(${x} ${y}) rotate(${rotation})`);
      readout.textContent = `Frame ${String(frame).padStart(2,'0')} / 12 · Drawing ${pose + 1}`;
      frames.forEach((button,index) => {
        const active = index + 1 === frame;
        button.setAttribute('aria-pressed', String(active));
        button.tabIndex = active ? 0 : -1;
      });
    };
    const pause = () => {
      if (timer !== null) { clearInterval(timer); timer = null; }
      play.textContent = '▶ Play';
      play.setAttribute('aria-pressed','false');
    };
    play.addEventListener('click', () => {
      if (timer !== null) { pause(); return; }
      play.textContent = 'Ⅱ Pause';
      play.setAttribute('aria-pressed','true');
      // Twelve frames at 12 fps; each of six drawings is held for two frames.
      timer = setInterval(() => { frame = frame % 12 + 1; render(); }, 1000 / 12);
    });
    $('[data-demo-reset]').addEventListener('click', () => { pause(); frame = 1; render(); });
    frames.forEach((button,index) => {
      button.addEventListener('click', () => { pause(); frame = index + 1; render(); });
      button.addEventListener('keydown', event => {
        let next;
        if (event.key === 'ArrowRight') next = (index + 1) % frames.length;
        if (event.key === 'ArrowLeft') next = (index - 1 + frames.length) % frames.length;
        if (event.key === 'Home') next = 0;
        if (event.key === 'End') next = frames.length - 1;
        if (next !== undefined) {
          event.preventDefault(); pause(); frame = next + 1; render(); frames[next].focus();
        }
      });
    });
    document.addEventListener('visibilitychange', () => { if (document.hidden) pause(); });
    if ('IntersectionObserver' in window) {
      const observer = new IntersectionObserver(entries => {
        if (entries.some(entry => !entry.isIntersecting)) pause();
      });
      observer.observe($('.shot-demo'));
    }
    render();
  }

  let scheduled = false;
  const readingProgress = () => {
    scheduled = false;
    const extent = document.documentElement.scrollHeight - innerHeight;
    document.documentElement.style.setProperty('--reading-progress', `${extent > 0 ? Math.min(100,scrollY / extent * 100) : 0}%`);
  };
  addEventListener('scroll', () => {
    if (!scheduled) { scheduled = true; requestAnimationFrame(readingProgress); }
  }, {passive: true});
  addEventListener('resize', readingProgress);
  readingProgress();
})();
