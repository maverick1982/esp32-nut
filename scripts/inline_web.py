import os
import base64

def inline_assets():
    data_dir = "data/www"
    index_file = os.path.join(data_dir, "index.html")
    
    if not os.path.exists(index_file):
        print(f"{index_file} not found.")
        return
        
    with open(index_file, "r", encoding="utf-8") as f:
        html = f.read()
        
    # Inline favicon
    favicon_path = os.path.join(data_dir, "favicon.ico")
    if os.path.exists(favicon_path):
        with open(favicon_path, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("utf-8")
            html = html.replace('<link rel="icon" type="image/x-icon" href="favicon.ico">', f'<link rel="icon" type="image/x-icon" href="data:image/x-icon;base64,{b64}">')

    # Inline CSS
    css_path = os.path.join(data_dir, "bundle.css")
    if os.path.exists(css_path):
        with open(css_path, "r", encoding="utf-8") as f:
            css = f.read()
            html = html.replace('<link rel="stylesheet" href="bundle.css?v=1.2.0">', f'<style>{css}</style>')

    # Inline Logo
    logo_path = os.path.join(data_dir, "logo.png")
    if os.path.exists(logo_path):
        with open(logo_path, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("utf-8")
            html = html.replace('<img src="logo.png" alt="Logo">', f'<img src="data:image/png;base64,{b64}" alt="Logo">')

    # Inline JS
    js_path = os.path.join(data_dir, "bundle.js")
    if os.path.exists(js_path):
        with open(js_path, "r", encoding="utf-8") as f:
            js = f.read()
            html = html.replace('<script src="bundle.js?v=1.2.0"></script>', f'<script>{js}</script>')
            
    out_file = os.path.join(data_dir, "index_inlined.html")
    with open(out_file, "w", encoding="utf-8") as f:
        f.write(html)
        
    print(f"Successfully created {out_file} (size: {len(html)} bytes)")

if __name__ == "__main__":
    inline_assets()
