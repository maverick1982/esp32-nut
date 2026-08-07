import os
import glob
import gzip

def embed_files():
    data_dir = "data/www"
    out_file = "include/network/web_assets.h"
    
    if not os.path.exists(data_dir):
        print(f"Directory {data_dir} not found. Skipping web embed.")
        return

    # Ensure output directory exists
    os.makedirs(os.path.dirname(out_file), exist_ok=True)

    with open(out_file, "w") as f:
        f.write("#ifndef WEB_ASSETS_H\n")
        f.write("#define WEB_ASSETS_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        
        files = glob.glob(os.path.join(data_dir, "**", "*.*"), recursive=True)
        for filepath in files:
            if not os.path.isfile(filepath):
                continue
            
            ext = os.path.splitext(filepath)[1].lower()
            if ext not in [".html", ".css", ".js", ".json", ".svg", ".png", ".ico", ".jpg", ".jpeg"]:
                continue
            
            with open(filepath, "rb") as bf:
                content = bf.read()
                
            # Compress content deterministically by setting mtime=0
            compressed = gzip.compress(content, mtime=0)
            
            rel_path = os.path.relpath(filepath, data_dir).replace("\\", "/")
            var_name = "web_asset_" + rel_path.replace("/", "_").replace(".", "_").replace("-", "_")
            
            f.write(f"const uint8_t {var_name}[] PROGMEM = {{\n")
            for i in range(0, len(compressed), 16):
                chunk = compressed[i:i+16]
                f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            f.write("};\n")
            f.write(f"const size_t {var_name}_len = {len(compressed)};\n\n")

        f.write("#endif // WEB_ASSETS_H\n")

embed_files()
