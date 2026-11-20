'use strict';

(function () {
  const sidebar = document.getElementById('sidebar');
  const menuBtn = document.getElementById('menuBtn');
  const backdrop = document.getElementById('backdrop');

  function setMenu(open) {
    if (!sidebar || !menuBtn) return;
    sidebar.classList.toggle('open', open);
    if (backdrop) backdrop.classList.toggle('show', open);
    menuBtn.setAttribute('aria-expanded', String(open));
    document.body.style.overflow = open && window.innerWidth <= 880 ? 'hidden' : '';
  }

  if (menuBtn) {
    menuBtn.addEventListener('click', () => setMenu(!sidebar.classList.contains('open')));
  }
  if (backdrop) backdrop.addEventListener('click', () => setMenu(false));
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') setMenu(false);
  });
  sidebar?.querySelectorAll('a').forEach((a) => a.addEventListener('click', () => setMenu(false)));
  window.addEventListener('resize', () => {
    if (window.innerWidth > 880) setMenu(false);
  });

  // Copy buttons on code blocks.
  document.querySelectorAll('.prose pre').forEach((pre) => {
    const code = pre.querySelector('code');
    if (!code) return;
    const btn = document.createElement('button');
    btn.className = 'copy-btn';
    btn.type = 'button';
    btn.textContent = 'Copy';
    btn.addEventListener('click', async () => {
      const text = code.innerText;
      try {
        await navigator.clipboard.writeText(text);
      } catch {
        const ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.left = '-9999px';
        document.body.appendChild(ta);
        ta.select();
        document.execCommand('copy');
        ta.remove();
      }
      btn.textContent = 'Copied';
      btn.classList.add('done');
      setTimeout(() => {
        btn.textContent = 'Copy';
        btn.classList.remove('done');
      }, 1400);
    });
    pre.appendChild(btn);
  });

  // Highlight the current section in the table of contents.
  const tocLinks = Array.from(document.querySelectorAll('.toc a[href^="#"]'));
  if (tocLinks.length) {
    const byId = new Map();
    for (const a of tocLinks) {
      const id = decodeURIComponent(a.getAttribute('href').slice(1));
      const el = document.getElementById(id);
      if (el) byId.set(el, a);
    }
    if ('IntersectionObserver' in window) {
      let current = null;
      const obs = new IntersectionObserver(
        (entries) => {
          for (const e of entries) {
            if (!e.isIntersecting) continue;
            const link = byId.get(e.target);
            if (!link || link === current) continue;
            if (current) current.classList.remove('active');
            current = link;
            current.classList.add('active');
          }
        },
        { rootMargin: '-90px 0px -70% 0px', threshold: 0 }
      );
      byId.forEach((_, el) => obs.observe(el));
    }
  }

  // Open external links in a new tab.
  document.querySelectorAll('.prose a[href^="http"]').forEach((a) => {
    try {
      if (new URL(a.href).host !== location.host) {
        a.target = '_blank';
        a.rel = 'noopener';
      }
    } catch {
      /* ignore */
    }
  });
})();
