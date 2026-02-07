"""Embed src/web files (HTML, CSS, JS) as PROGMEM strings before build."""
from __future__ import annotations

import json
from pathlib import Path

try:
    Import("env")  # type: ignore  # Provided by PlatformIO
except Exception:  # Running outside PlatformIO (manual invocation)
    env = {"PROJECT_DIR": str(Path(__file__).resolve().parents[1])}

PROJECT_DIR = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
WEB_DIR = PROJECT_DIR / "src" / "web"
OUTPUT_DIR = PROJECT_DIR / "src" / "generated"
OUTPUT_FILE = OUTPUT_DIR / "web_dashboard.h"

# Files to embed
files_to_embed = {
    "index": WEB_DIR / "index.html",
    "visualization": WEB_DIR / "visualization.html",
    "email": WEB_DIR / "email.html",
    "css": WEB_DIR / "dashboard.css",
    "core_js": WEB_DIR / "core.js",
    "collection_js": WEB_DIR / "collection.js",
    "viz_js": WEB_DIR / "visualization.js",
}

# Check all files exist
for name, path in files_to_embed.items():
    if not path.exists():
        raise FileNotFoundError(f"{name.upper()} file not found: {path}")

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Read and embed all files
index_content = files_to_embed["index"].read_text(encoding="utf-8")
viz_content = files_to_embed["visualization"].read_text(encoding="utf-8")
email_content = files_to_embed["email"].read_text(encoding="utf-8")
css_content = files_to_embed["css"].read_text(encoding="utf-8")
core_js_content = files_to_embed["core_js"].read_text(encoding="utf-8")
collection_js_content = files_to_embed["collection_js"].read_text(encoding="utf-8")
viz_js_content = files_to_embed["viz_js"].read_text(encoding="utf-8")

index_literal = json.dumps(index_content)
viz_literal = json.dumps(viz_content)
email_literal = json.dumps(email_content)
css_literal = json.dumps(css_content)
core_js_literal = json.dumps(core_js_content)
collection_js_literal = json.dumps(collection_js_content)
viz_js_literal = json.dumps(viz_js_content)

header = f"""#pragma once
#include <pgmspace.h>

namespace webui {{
static const char kIndexHtml[] PROGMEM = {index_literal};
static const char kVisualizationHtml[] PROGMEM = {viz_literal};
static const char kEmailHtml[] PROGMEM = {email_literal};
static const char kDashboardCss[] PROGMEM = {css_literal};
static const char kCoreJs[] PROGMEM = {core_js_literal};
static const char kCollectionJs[] PROGMEM = {collection_js_literal};
static const char kVisualizationJs[] PROGMEM = {viz_js_literal};
}}
"""

OUTPUT_FILE.write_text(header, encoding="utf-8")
print(f"[embed_web] Embedded files -> {OUTPUT_FILE}")
