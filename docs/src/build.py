#!/usr/bin/env python3
"""Build the ISO-style PDF documents in docs/ from the Markdown sources in docs/src/.

    pip install markdown beautifulsoup4 weasyprint
    python3 docs/src/build.py            # every VNT-*.md
    python3 docs/src/build.py VNT-001    # one document

Writing conventions in the .md sources
  # Foreword / # Introduction        front matter (pages numbered i, ii, ...)
  # 1 Scope, ## 7.1 Printing        numbered clauses and subclauses (numbers are written by hand)
  ### 3.1 + **term** + definition    terms and definitions entry
  # Annex A (normative) Title       annex heading
  # Bibliography                     bibliography
  NOTE ... / EXAMPLE ... / WARNING — ...   note, example and warning paragraphs
  Table 1 — Title  (paragraph right above a table)
  ![](figures/x.svg) followed by "Figure 1 — Title"   figure with caption
"""
import re
import sys
from pathlib import Path

import markdown
from bs4 import BeautifulSoup
from weasyprint import CSS, HTML

SRC = Path(__file__).resolve().parent
OUT = SRC.parent
PAPER = {"Letter": "letter", "A4": "A4"}


def front_matter(text):
    m = re.match(r"---\n(.*?)\n---\n", text, re.S)
    meta = dict(re.findall(r"^(\w+):\s*(.*)$", m.group(1), re.M))
    return meta, text[m.end():]


def slug(s):
    return re.sub(r"[^a-z0-9]+", "-", s.lower()).strip("-")


def process(soup):
    """turn plain Markdown HTML into ISO-styled structure; returns toc entries"""
    toc = []
    for h in soup.find_all(["h1", "h2", "h3", "h4"]):
        text = h.get_text().strip()
        m_annex = re.match(r"Annex ([A-Z]) \((normative|informative)\) (.+)", text)
        m_num = re.match(r"((?:\d+|[A-Z])(?:\.\d+)*)\s+(.+)", text)
        if h.name == "h1" and m_annex:
            letter, kind, title = m_annex.groups()
            h.clear()
            h["class"] = ["annex"]
            h["id"] = f"annex-{letter.lower()}"
            for cls, t in (("label", f"Annex {letter}"), ("kind", f"({kind})"), ("title", title)):
                s = soup.new_tag("span", attrs={"class": cls})
                s.string = t
                h.append(s)
            toc.append((1, f"Annex {letter} ({kind}) {title}", h["id"]))
        elif h.name == "h3" and re.fullmatch(r"\d+(\.\d+)+", text):     # term entry number
            h.name = "p"
            h["class"] = ["term-num"]
            nxt = h.find_next_sibling()
            if nxt is not None and nxt.name == "p":
                nxt["class"] = ["term"]
                d = nxt.find_next_sibling()
                while d is not None and d.name == "p" and not d.get("class"):
                    d["class"] = ["term-note"] if d.get_text().startswith("Note ") else ["term-def"]
                    d = d.find_next_sibling()
        elif m_num and h.name in ("h1", "h2", "h3"):
            num, title = m_num.groups()
            h.clear()
            sn = soup.new_tag("span", attrs={"class": "num"})
            sn.string = num
            h.append(sn)
            h.append(title)
            h["id"] = "clause-" + num.replace(".", "-").lower()
            if h.name in ("h1", "h2"):
                toc.append((1 if h.name == "h1" else 2, f"{num} {title}", h["id"]))
        elif h.name == "h1":
            h["id"] = slug(text)
            if text == "Bibliography":
                h["class"] = ["biblio"]
            toc.append((1, text, h["id"]))
    for p in soup.find_all("p"):
        t = p.get_text()
        if re.match(r"(NOTE|EXAMPLE)( \d+)?\s", t):
            tag = re.match(r"(NOTE|EXAMPLE)( \d+)?", t).group(0)
            p["class"] = ["note" if tag.startswith("NOTE") else "example"]
            first = p.contents[0]
            if isinstance(first, str):
                first.replace_with(first[len(tag):].lstrip())
                s = soup.new_tag("span", attrs={"class": "tag"})
                s.string = tag
                p.insert(0, s)
        elif t.startswith("WARNING"):
            p["class"] = ["warning"]
        elif re.match(r"Table [A-Z0-9.]+ — ", t):
            p["class"] = ["table-title"]
        elif p.find("img") and len(p.get_text(strip=True)) == 0:
            cap = p.find_next_sibling("p")
            fig = soup.new_tag("figure")
            for img in p.find_all("img"):
                fig.append(img.extract())
            if cap is not None and re.match(r"Figure [A-Z0-9.]+ — ", cap.get_text()):
                fc = soup.new_tag("figcaption")
                fc.extend(list(cap.contents))
                cap.decompose()
                fig.append(fc)
            p.replace_with(fig)
    return toc


def build(md_path, pages=None):
    meta, body_md = front_matter(md_path.read_text(encoding="utf-8"))
    html_body = markdown.markdown(body_md, extensions=["tables", "attr_list", "md_in_html", "sane_lists", "smarty"])
    soup = BeautifulSoup(f"<div id='all'>{html_body}</div>", "html.parser")
    toc = process(soup)
    # split into front matter (Foreword, Introduction) and body (from clause 1)
    front, body, cur = [], [], None
    for el in list(soup.find(id="all").children):
        if getattr(el, "name", None) == "h1":
            cur = front if el.get("id") in ("foreword", "introduction") else body
        if cur is not None:
            cur.append(str(el))
    docref = f"{meta['id']}:{meta['year']}(E)"
    title_full = f"{meta['title_intro']} — {meta['title_main']} — {meta['title_part']}"
    toc_items = []
    for level, text, ident in toc:
        cls = "roman" if ident in ("foreword", "introduction") else ""
        toc_items.append(f'<li class="l{level}"><a class="{cls}" href="#{ident}">{text}</a></li>')
    pages_txt = str(pages) if pages else "—"
    cover = f"""
<section class="cover">
  <span class="docref">{docref}</span><span class="company">{meta['company']}</span><span class="year">{meta['year']}</span>
  <div class="cover-top">
    <div class="doctype">HARDWARE DOCUMENT</div>
    <div class="docid">{meta['id']}</div>
    <div class="edition">{meta['edition']}</div>
    <div class="date">{meta['date']}</div>
  </div>
  <div class="cover-title">
    <div class="t">{title_full}</div>
    <div class="sub">Applies to: {meta['applies']}</div>
  </div>
  <div class="cover-bottom">
    <div class="refbox">Reference number<br><span class="big">{docref}</span></div>
    <table class="titleblock">
      <colgroup><col style="width:30%"><col style="width:42%"><col style="width:28%"></colgroup>
      <tr><td colspan="3" class="tb-company">{meta['company']}</td></tr>
      <tr><td colspan="3">Sheet: {meta['title_part']}</td></tr>
      <tr><td colspan="3">File: {meta['output']}</td></tr>
      <tr><td colspan="3" class="title">Title: {meta['title_intro']} — {meta['title_main']}</td></tr>
      <tr><td>Size: {meta['paper']}</td><td>Date: {meta['date']}</td><td class="rev">Rev: {meta['rev']}</td></tr>
      <tr><td colspan="2">Document: {meta['id']}</td><td>Pages: {pages_txt}</td></tr>
    </table>
  </div>
</section>"""
    copyright_page = f"""
<section class="front"><div class="copyright">
  <p class="head">CONTROLLED HARDWARE DOCUMENT</p>
  <p>© {meta['company']} {meta['year']}. All rights reserved. This document is part of the hardware design records
  kept in the team repository. The current edition is the one in the repository at the matching hardware revision tag;
  printed copies are uncontrolled.</p>
  <p>{meta['company']}</p>
</div></section>"""
    contents = f"""<section class="front toc"><h1 class="unnumbered">Contents</h1><ul>{''.join(toc_items)}</ul></section>"""
    html = (f"<!doctype html><html lang='en'><head><meta charset='utf-8'><title>{docref} {title_full}</title>"
            f"<meta name='author' content='{meta['company']}'></head><body>{cover}{copyright_page}{contents}"
            f"<section class='front'>{''.join(front)}</section><section class='body'>{''.join(body)}</section>"
            f"</body></html>")
    css = [CSS(filename=str(SRC / "iso.css")), CSS(string=f"@page {{ size: {PAPER[meta['paper']]}; }}")]
    doc = HTML(string=html, base_url=str(SRC)).render(stylesheets=css)
    return meta, doc


def main(only=None):
    for md in sorted(SRC.glob("VNT-*.md")):
        if only and not md.stem.startswith(only):
            continue
        meta, doc = build(md)
        n = len(doc.pages)
        meta, doc = build(md, pages=n)                  # second pass: page count in the title block
        out = OUT / meta["output"]
        doc.write_pdf(out)
        print(f"{out.name}: {len(doc.pages)} pages")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
