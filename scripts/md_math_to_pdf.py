#!/usr/bin/env python3
"""Markdown + LaTeX math → PDF (WeasyPrint)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

import markdown
from latex2mathml.converter import convert as latex_to_mathml
from weasyprint import CSS, HTML


DISPLAY_RE = re.compile(r"\$\$(.+?)\$\$", re.DOTALL)
INLINE_RE = re.compile(r"(?<!\$)\$(?!\$)(.+?)(?<!\$)\$(?!\$)", re.DOTALL)


def latex_to_mathml_safe(expr: str, display: bool) -> str:
    expr = expr.strip()
    try:
        mathml = latex_to_mathml(expr, display="block" if display else "inline")
    except Exception:
        escaped = (
            expr.replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
        )
        tag = "div" if display else "span"
        return f'<{tag} class="math-fallback">{escaped}</{tag}>'
    return mathml


def replace_math(text: str) -> str:
    def disp(m: re.Match[str]) -> str:
        return latex_to_mathml_safe(m.group(1), display=True)

    def inline(m: re.Match[str]) -> str:
        return latex_to_mathml_safe(m.group(1), display=False)

    text = DISPLAY_RE.sub(disp, text)
    text = INLINE_RE.sub(inline, text)
    return text


CSS_TEXT = """
@page {
  size: A4;
  margin: 2.2cm 2cm 2.2cm 2cm;
  @bottom-center {
    content: counter(page);
    font-size: 9pt;
    color: #666;
  }
}
body {
  font-family: "DejaVu Serif", "Liberation Serif", "Noto Serif", serif;
  font-size: 10.5pt;
  line-height: 1.45;
  color: #1a1a1a;
}
h1 {
  font-size: 18pt;
  border-bottom: 2px solid #333;
  padding-bottom: 0.3em;
  margin-top: 0;
}
h2 {
  font-size: 14pt;
  border-bottom: 1px solid #aaa;
  padding-bottom: 0.2em;
  margin-top: 1.4em;
  page-break-after: avoid;
}
h3 {
  font-size: 12pt;
  margin-top: 1.1em;
  page-break-after: avoid;
}
h4 { font-size: 11pt; margin-top: 0.9em; }
p { margin: 0.5em 0; text-align: justify; }
table {
  border-collapse: collapse;
  width: 100%;
  margin: 0.8em 0;
  font-size: 9.5pt;
  page-break-inside: avoid;
}
th, td {
  border: 1px solid #bbb;
  padding: 0.35em 0.5em;
  vertical-align: top;
}
th { background: #f0f0f0; font-weight: bold; }
code, pre {
  font-family: "DejaVu Sans Mono", "Liberation Mono", monospace;
  font-size: 8.5pt;
}
pre {
  background: #f7f7f7;
  border: 1px solid #ddd;
  padding: 0.6em;
  overflow-wrap: anywhere;
  white-space: pre-wrap;
  page-break-inside: avoid;
}
hr { border: none; border-top: 1px solid #ccc; margin: 1.2em 0; }
em { font-style: italic; }
strong { font-weight: bold; }
.math-fallback {
  font-family: "DejaVu Sans Mono", monospace;
  background: #fff8e1;
  padding: 0.1em 0.3em;
}
math { font-size: 105%; }
blockquote {
  border-left: 3px solid #ccc;
  margin-left: 0;
  padding-left: 1em;
  color: #444;
}
"""


def md_to_pdf(md_path: Path, pdf_path: Path) -> None:
    raw = md_path.read_text(encoding="utf-8")
    with_math = replace_math(raw)
    html_body = markdown.markdown(
        with_math,
        extensions=["tables", "fenced_code", "nl2br", "sane_lists"],
    )
    doc = f"""<!DOCTYPE html>
<html lang="tr">
<head>
  <meta charset="utf-8"/>
  <title>Kontrol Algoritması Teknik Rapor</title>
</head>
<body>
{html_body}
</body>
</html>"""
    HTML(string=doc, base_url=str(md_path.parent)).write_pdf(
        str(pdf_path),
        stylesheets=[CSS(string=CSS_TEXT)],
    )


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Kullanım: {sys.argv[0]} <input.md> <output.pdf>", file=sys.stderr)
        return 1
    md_path = Path(sys.argv[1]).resolve()
    pdf_path = Path(sys.argv[2]).resolve()
    if not md_path.is_file():
        print(f"Dosya bulunamadı: {md_path}", file=sys.stderr)
        return 1
    md_to_pdf(md_path, pdf_path)
    print(f"PDF oluşturuldu: {pdf_path} ({pdf_path.stat().st_size // 1024} KB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
