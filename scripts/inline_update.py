import os

def inline_update():
    data_dir = "data/www"
    update_file = os.path.join(data_dir, "update.html")
    
    if not os.path.exists(update_file):
        print(f"{update_file} not found.")
        return
        
    with open(update_file, "r", encoding="utf-8") as f:
        html = f.read()
        
    # Read bundle.css and shared_ota.css
    css_content = ""
    for css in ["bundle.css", "shared_ota.css"]:
        path = os.path.join(data_dir, css)
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                css_content += f.read() + "\n"

    # Read fflate.min.js
    fflate_content = ""
    fflate_path = os.path.join(data_dir, "fflate.min.js")
    if os.path.exists(fflate_path):
        with open(fflate_path, "r", encoding="utf-8") as f:
            fflate_content = f.read()

    # Replace tags
    html = html.replace('<link rel="stylesheet" href="shared.css?v=1.1.1">', f'<style>{css_content}</style>')
    html = html.replace('<link rel="stylesheet" href="mobile.css?v=1.1.1">', '')
    html = html.replace('<script src="fflate.min.js?v=1.1.1"></script>', f'<script>{fflate_content}</script>')
    
    out_file = os.path.join(data_dir, "update_inlined.html")
    with open(out_file, "w", encoding="utf-8") as f:
        f.write(html)
        
    print(f"Successfully created {out_file} (size: {len(html)} bytes)")

if __name__ == "__main__":
    inline_update()
