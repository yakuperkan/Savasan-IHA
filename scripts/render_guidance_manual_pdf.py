#!/usr/bin/env python3
"""GUIDANCE_USER_MANUAL.md -> PDF (weasyprint). Türkçe karakter destekli."""

from __future__ import annotations

import sys
from pathlib import Path

import markdown
from weasyprint import CSS, HTML

ROOT = Path(__file__).resolve().parents[1]
MD_PATH = ROOT / "docs" / "GUIDANCE_USER_MANUAL.md"
PDF_PATH = ROOT / "docs" / "GUIDANCE_USER_MANUAL.pdf"

STYLES = """
@page {
  size: A4;
  margin: 18mm 16mm 20mm 16mm;
}
body {
  font-family: "DejaVu Sans", "Liberation Sans", sans-serif;
  font-size: 10pt;
  line-height: 1.45;
  color: #1a1a1a;
}
h1 { font-size: 18pt; margin-top: 0; border-bottom: 2px solid #333; padding-bottom: 6px; }
h2 { font-size: 13pt; margin-top: 18px; color: #222; }
h3 { font-size: 11pt; margin-top: 14px; }
code, pre { font-family: "DejaVu Sans Mono", monospace; font-size: 8.5pt; }
pre {
  background: #f4f4f4;
  border: 1px solid #ddd;
  padding: 8px;
  white-space: pre-wrap;
  word-break: break-word;
}
table {
  border-collapse: collapse;
  width: 100%;
  font-size: 9pt;
  margin: 8px 0;
}
th, td {
  border: 1px solid #ccc;
  padding: 4px 6px;
  text-align: left;
  vertical-align: top;
}
th { background: #eee; }
hr { border: none; border-top: 1px solid #ccc; margin: 16px 0; }
"""


def main() -> int:
    if not MD_PATH.is_file():
        print(f"HATA: bulunamadi: {MD_PATH}", file=sys.stderr)
        return 1

    md_text = MD_PATH.read_text(encoding="utf-8")
    html_body = markdown.markdown(
        md_text,
        extensions=["tables", "fenced_code", "toc"],
    )
    html_doc = f"""<!DOCTYPE html>
<html lang="tr">
<head><meta charset="utf-8"><title>Savasan IHA Gudum Kilavuzu</title></head>
<body>{html_body}</body>
</html>"""

    HTML(string=html_doc, base_url=str(MD_PATH.parent)).write_pdf(
        str(PDF_PATH),
        stylesheets=[CSS(string=STYLES)],
    )
    print(f"OK: {PDF_PATH} ({PDF_PATH.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
