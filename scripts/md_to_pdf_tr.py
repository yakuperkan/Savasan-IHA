#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Markdown -> PDF (Turkce karakter destekli, WeasyPrint + DejaVu Sans)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import markdown
from weasyprint import CSS, HTML


CSS_TR = """
@page {
  size: A4;
  margin: 2cm 1.8cm 2.2cm 1.8cm;
  @bottom-center {
    content: counter(page);
    font-family: "DejaVu Sans", sans-serif;
    font-size: 9pt;
    color: #555;
  }
}
body {
  font-family: "DejaVu Sans", "Liberation Sans", sans-serif;
  font-size: 10pt;
  line-height: 1.45;
  color: #111;
}
h1 {
  font-size: 18pt;
  border-bottom: 2px solid #333;
  padding-bottom: 6px;
  page-break-after: avoid;
}
h2 {
  font-size: 14pt;
  margin-top: 1.2em;
  color: #1a1a1a;
  page-break-after: avoid;
}
h3 {
  font-size: 12pt;
  page-break-after: avoid;
}
p, li { orphans: 3; widows: 3; }
code {
  font-family: "DejaVu Sans Mono", monospace;
  font-size: 8.5pt;
  background: #f0f0f0;
  padding: 1px 3px;
}
pre {
  font-family: "DejaVu Sans Mono", monospace;
  font-size: 8pt;
  background: #f4f4f4;
  padding: 8px 10px;
  border: 1px solid #ddd;
  white-space: pre-wrap;
  word-wrap: break-word;
  page-break-inside: avoid;
}
table {
  border-collapse: collapse;
  width: 100%;
  font-size: 8pt;
  margin: 10px 0;
  page-break-inside: avoid;
}
th, td {
  border: 1px solid #bbb;
  padding: 4px 5px;
  text-align: left;
  vertical-align: top;
}
th { background: #e6e6e6; font-weight: bold; }
tr:nth-child(even) td { background: #fafafa; }
hr {
  border: none;
  border-top: 1px solid #ccc;
  margin: 1.2em 0;
}
ul, ol { padding-left: 1.3em; }
li { margin: 0.2em 0; }
strong { font-weight: bold; }
"""


def md_to_pdf(input_md: Path, output_pdf: Path, title: str | None = None) -> None:
    text = input_md.read_text(encoding="utf-8")
    body = markdown.markdown(
        text,
        extensions=["tables", "fenced_code", "nl2br"],
    )
    doc_title = title or input_md.stem.replace("_", " ")
    html = f"""<!DOCTYPE html>
<html lang="tr">
<head>
  <meta charset="utf-8"/>
  <title>{doc_title}</title>
</head>
<body>
{body}
</body>
</html>"""
    HTML(string=html, base_url=str(input_md.parent)).write_pdf(
        str(output_pdf),
        stylesheets=[CSS(string=CSS_TR)],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Markdown dosyasini Turkce uyumlu PDF'e cevirir.")
    parser.add_argument("input", type=Path, help="Girdi .md dosyasi")
    parser.add_argument("-o", "--output", type=Path, help="Cikti .pdf (varsayilan: girdi ile ayni ad)")
    parser.add_argument("--title", type=str, default=None, help="PDF basligi")
    args = parser.parse_args()
    if not args.input.is_file():
        print(f"HATA: dosya bulunamadi: {args.input}", file=sys.stderr)
        return 1
    out = args.output or args.input.with_suffix(".pdf")
    md_to_pdf(args.input, out, args.title)
    print(f"OK: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
