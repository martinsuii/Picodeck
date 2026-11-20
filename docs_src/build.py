#!/usr/bin/env python3
"""Build the static PicoDeck docs site from the markdown sources.

    cd docs_src && python3 build.py

Requires: pip install markdown
"""

import pathlib
import re
import markdown

HERE = pathlib.Path(__file__).resolve().parent
SITE = HERE.parent / "docs"
REPO = "https://github.com/martinsuii/Picodeck"
PAGES_URL = "https://martinsuii.github.io/Picodeck"


def version():
    p = HERE.parent / "launcher" / "src" / "main.h"
    try:
        m = re.search(r'LAUNCHER_VERSION_STR\s+"([^"]+)"', p.read_text())
        return m.group(1) if m else "1.0.0"
    except OSError:
        return "1.0.0"


def fix_links(md):
    # Code links to the repository, not the published site.
    replacements = {
        "../apps_sdk/README.md": f"{REPO}/blob/main/apps_sdk/README.md",
        "../apps_sdk/examples/hello": f"{REPO}/tree/main/apps_sdk/examples/hello",
        "../apps_sdk/examples/wifiscan": f"{REPO}/tree/main/apps_sdk/examples/wifiscan",
        "../apps_sdk/examples/nettest": f"{REPO}/tree/main/apps_sdk/examples/nettest",
        "../themes/README.md": f"{REPO}/blob/main/themes/README.md",
        "../companion/README.md": f"{REPO}/blob/main/companion/README.md",
    }
    for a, b in replacements.items():
        md = md.replace(a, b)

    # Local .md page links -> .html
    page_map = {
        "README.md": "index.html",
        "using-picodeck.md": "using.html",
        "making-apps.md": "apps.html",
        "contributing.md": "contributing.html",
    }

    def repl(m):
        target = m.group(1)
        anchor = m.group(2) or ""
        name = target.split("/")[-1]
        if name in page_map:
            return f"]({page_map[name]}{anchor})"
        return m.group(0)

    md = re.sub(r"\]\(([^)#]+?\.md)(#[^)]*)?\)", repl, md)
    return md


def readme_first_h1(md):
    return re.sub(r"^# .*?\n+", "", md, count=1)


PAGES = [
    {
        "slug": "index",
        "source": "README.md",
        "title": "Documentation",
        "nav": "Overview",
        "crumb": "Overview",
        "description": "PicoDeck documentation: install, use, build apps and contribute.",
        "subtitle": "PicoDeck turns the PicoPad into a tiny app computer.",
        "hero": True,
    },
    {
        "slug": "using",
        "source": "using-picodeck.md",
        "title": "Using PicoDeck",
        "nav": "Using PicoDeck",
        "crumb": "Using PicoDeck",
        "description": "Install, update, play games, connect WiFi, run apps and recover a PicoPad.",
        "subtitle": "Install, update, play games, connect WiFi, run apps and recover.",
    },
    {
        "slug": "apps",
        "source": "making-apps.md",
        "title": "Making apps",
        "nav": "Making apps",
        "crumb": "Making apps",
        "description": "Build a custom PicoDeck app with the SDK and app API.",
        "subtitle": "The SDK, the app API, packages, examples and limits.",
    },
    {
        "slug": "contributing",
        "source": "contributing.md",
        "title": "Contributing",
        "nav": "Contributing",
        "crumb": "Contributing",
        "description": "Build PicoDeck, understand the flash layout and ship a release.",
        "subtitle": "Builds, flash layout, safety invariants, testing and releases.",
    },
]

NAV_DOCS = """<div class="side-group">
  <div class="side-title">Documentation</div>
  <a class="{a_index}" href="index.html"><span>▦</span> Overview</a>
  <a class="{a_using}" href="using.html"><span>▤</span> Using PicoDeck</a>
  <a class="{a_apps}" href="apps.html"><span>⌘</span> Making apps</a>
  <a class="{a_contributing}" href="contributing.html"><span>⚙</span> Contributing</a>
</div>"""

NAV_LINKS = """<div class="side-group">
  <div class="side-title">Project</div>
  <a href="https://github.com/martinsuii/Picodeck/releases" target="_blank" rel="noopener"><span>↓</span> Releases</a>
  <a href="https://github.com/martinsuii/Picodeck" target="_blank" rel="noopener"><span>⌂</span> Source</a>
  <a href="http://assets.msuii.dev/picopadapps/" target="_blank" rel="noopener"><span>◈</span> App catalogue</a>
  <a href="https://github.com/MakerClassCZ/picopad-arcade-launcher" target="_blank" rel="noopener"><span>↗</span> Base project</a>
</div>"""

TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="color-scheme" content="dark">
<meta name="description" content="{description}">
<meta property="og:title" content="{title} · PicoDeck docs">
<meta property="og:description" content="{description}">
<meta property="og:type" content="website">
<link rel="canonical" href="{pages_url}/{slug}.html">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="style.css">
<title>{title} · PicoDeck docs</title>
</head>
<body>
<a class="skip" href="#main">Skip to content</a>

<header class="topbar">
  <button class="menu-btn" id="menuBtn" aria-label="Open navigation" aria-controls="sidebar" aria-expanded="false">
    <span></span><span></span><span></span>
  </button>
  <a class="brand" href="index.html">
    <span class="brand-mark">P</span>
    <span class="brand-name">PicoDeck</span>
    <span class="brand-sub">docs</span>
  </a>
  <div class="top-spacer"></div>
  <span class="version">v{version}</span>
  <a class="top-link" href="https://github.com/martinsuii/Picodeck" target="_blank" rel="noopener">GitHub ↗</a>
  <a class="btn btn-small" href="https://github.com/martinsuii/Picodeck/releases">Download</a>
</header>

<div class="shell">
  <aside class="sidebar" id="sidebar">
    {nav_docs}
    {nav_links}
    <div class="side-foot">
      <div>PicoDeck <span>v{version}</span></div>
      <div class="muted">Base launcher by MakerClassCZ</div>
    </div>
  </aside>

  <main class="content" id="main">
    <div class="crumbs"><a href="index.html">Docs</a><span>/</span>{crumb}</div>
    <div class="page-head">
      <h1>{title}</h1>
      <p class="subtitle">{subtitle}</p>
    </div>
    {hero}
    <article class="prose">
{content}
    </article>
    {pagenav}
  </main>

  <aside class="toc">
    <div class="toc-title">On this page</div>
    {toc}
  </aside>
</div>

<footer class="footer">
  <div>PicoDeck <strong>v{version}</strong> · docs</div>
  <div class="muted">Derived from <a href="https://github.com/MakerClassCZ/picopad-arcade-launcher" target="_blank" rel="noopener">PicoPad Arcade Launcher by MakerClassCZ</a></div>
</footer>

<div class="backdrop" id="backdrop"></div>
<script src="app.js"></script>
</body>
</html>
"""

HERO = """<section class="hero">
  <div class="hero-kicker">PicoPad Arcade · RP2040 · Pico W</div>
  <h2>Games, WiFi and custom apps.<br><span>In your pocket.</span></h2>
  <p>PicoDeck is the launcher and app platform for the PicoPad. Flash games, run the radio natively, install apps from the SD card or the store, and keep a recovery image that always boots.</p>
  <div class="hero-actions">
    <a class="btn" href="using.html">Get started</a>
    <a class="btn btn-ghost" href="apps.html">Make an app</a>
    <a class="btn btn-ghost" href="https://github.com/martinsuii/Picodeck/releases">Download v{version}</a>
  </div>
</div>"""


def pagenav(index):
    prev = PAGES[(index - 1) % len(PAGES)]
    nxt = PAGES[(index + 1) % len(PAGES)]
    return f"""<nav class="pagenav">
  <a class="page-prev" href="{prev['slug']}.html"><span>Previous</span><strong>{prev['title']}</strong></a>
  <a class="page-next" href="{nxt['slug']}.html"><span>Next</span><strong>{nxt['title']}</strong></a>
</nav>"""


def main():
    ver = version()
    SITE.mkdir(parents=True, exist_ok=True)

    for i, page in enumerate(PAGES):
        src = (HERE / page["source"]).read_text(encoding="utf-8")
        md = readme_first_h1(fix_links(src))

        conv = markdown.Markdown(
            extensions=["fenced_code", "tables", "toc", "sane_lists", "attr_list"],
            extension_configs={
                "toc": {"toc_depth": "2-3", "permalink": "", "toc_class": "toc-inner"},
            },
        )
        content = conv.convert(md)
        toc = conv.toc

        active = {f"a_{p['slug']}": "" for p in PAGES}
        active[f"a_{page['slug']}"] = "active"
        nav_docs = NAV_DOCS.format(**active)

        html = TEMPLATE.format(
            title=page["title"],
            description=page["description"],
            subtitle=page["subtitle"],
            crumb=page["crumb"],
            slug=page["slug"],
            version=ver,
            nav_docs=nav_docs,
            nav_links=NAV_LINKS,
            hero=(HERO.format(version=ver) if page.get("hero") else ""),
            content=content,
            toc=toc,
            pagenav=pagenav(i),
            pages_url=PAGES_URL,
        )
        (SITE / f"{page['slug']}.html").write_text(html, encoding="utf-8")
        print(f"built docs/{page['slug']}.html")


if __name__ == "__main__":
    main()
