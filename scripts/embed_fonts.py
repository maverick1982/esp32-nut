import urllib.request
import base64
import re

url = "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=Orbitron:wght@400;700&family=Share+Tech+Mono&display=swap"

req = urllib.request.Request(
    url, 
    headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36'}
)

with urllib.request.urlopen(req) as response:
    css_content = response.read().decode('utf-8')

# The CSS contains blocks like:
# /* latin */
# @font-face {
#   font-family: 'Inter';
#   ...
# }

blocks = css_content.split('/* ')
new_css_blocks = []

for block in blocks:
    if not block.strip():
        continue
    # Only keep latin subset (not latin-ext)
    if block.startswith('latin */'):
        # extract the url
        src_match = re.search(r'url\((.*?)\)', block)
        if src_match:
            font_url = src_match.group(1)
            print(f"Downloading {font_url}...")
            
            req_font = urllib.request.Request(font_url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req_font) as font_resp:
                font_data = font_resp.read()
                b64_data = base64.b64encode(font_data).decode('utf-8')
                mime = "font/woff2" if font_url.endswith(".woff2") else "font/woff"
                data_uri = f"data:{mime};charset=utf-8;base64,{b64_data}"
                
                # replace url with data URI
                block = block.replace(font_url, data_uri)
                new_css_blocks.append('/* ' + block)

final_font_css = ''.join(new_css_blocks)

bundle_path = r"d:\esp32-nut\data\www\bundle.css"
with open(bundle_path, 'r', encoding='utf-8') as f:
    bundle_content = f.read()

with open(bundle_path, 'w', encoding='utf-8') as f:
    f.write("/* --- Embedded Google Fonts (Latin Subset) --- */\n")
    f.write(final_font_css)
    f.write("\n/* --- End Embedded Fonts --- */\n\n")
    f.write(bundle_content)

print("Fonts successfully embedded into bundle.css!")
