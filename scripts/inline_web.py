import os
import base64

DATA_DIR = "data/www"


def inline_page(src_name, js_name, out_name):
    """Inline favicon, bundle.css, logo and the page script into a single HTML file.

    The placeholders below must match the tags used in the source page exactly.
    """
    src_file = os.path.join(DATA_DIR, src_name)

    if not os.path.exists(src_file):
        print(f"{src_file} not found.")
        return

    with open(src_file, "r", encoding="utf-8") as f:
        html = f.read()

    # Inline favicon
    favicon_path = os.path.join(DATA_DIR, "favicon.ico")
    if os.path.exists(favicon_path):
        with open(favicon_path, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("utf-8")
            html = html.replace('<link rel="icon" type="image/x-icon" href="favicon.ico">', f'<link rel="icon" type="image/x-icon" href="data:image/x-icon;base64,{b64}">')

    # Inline CSS
    css_path = os.path.join(DATA_DIR, "bundle.css")
    if os.path.exists(css_path):
        with open(css_path, "r", encoding="utf-8") as f:
            css = f.read()
            html = html.replace('<link rel="stylesheet" href="bundle.css?v=1.2.0">', f'<style>{css}</style>')

    # Inline Logo
    logo_path = os.path.join(DATA_DIR, "logo.png")
    if os.path.exists(logo_path):
        with open(logo_path, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("utf-8")
            html = html.replace('<img src="logo.png" alt="Logo">', f'<img src="data:image/png;base64,{b64}" alt="Logo">')

    # Inline JS
    js_path = os.path.join(DATA_DIR, js_name)
    if os.path.exists(js_path):
        with open(js_path, "r", encoding="utf-8") as f:
            js = f.read()
            html = html.replace(f'<script src="{js_name}?v=1.2.0"></script>', f'<script>{js}</script>')

    out_file = os.path.join(DATA_DIR, out_name)
    with open(out_file, "w", encoding="utf-8") as f:
        f.write(html)

    print(f"Successfully created {out_file} (size: {len(html)} bytes)")


def inline_assets():
    inline_page("index.html", "bundle.js", "index_inlined.html")
    # US-061: /login page
    inline_page("login.html", "login.js", "login_inlined.html")


if __name__ == "__main__":
    inline_assets()
