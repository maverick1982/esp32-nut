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

    import hashlib
    
    # Calculate hash of all source files
    hasher = hashlib.md5()
    files = glob.glob(os.path.join(data_dir, "**", "*.*"), recursive=True)
    files.sort()
    
    for filepath in files:
        if not os.path.isfile(filepath):
            continue
        ext = os.path.splitext(filepath)[1].lower()
        if ext not in [".html", ".css", ".js", ".json", ".svg", ".png", ".ico", ".jpg", ".jpeg"]:
            continue
        with open(filepath, "rb") as bf:
            hasher.update(bf.read())
            
    current_hash = hasher.hexdigest()
    hash_file = "include/network/.web_assets.hash"
    
    # If hash matches, skip generation
    if os.path.exists(hash_file) and os.path.exists(out_file):
        with open(hash_file, "r") as hf:
            if hf.read().strip() == current_hash:
                print("Web assets unchanged. Skipping generation.")
                return

    with open(out_file, "w") as f:
        f.write("#ifndef WEB_ASSETS_H\n")
        f.write("#define WEB_ASSETS_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        
        for filepath in files:
            if not os.path.isfile(filepath):
                continue
            
            ext = os.path.splitext(filepath)[1].lower()
            if ext not in [".html", ".css", ".js", ".json", ".svg", ".png", ".ico", ".jpg", ".jpeg"]:
                continue
            
            with open(filepath, "rb") as bf:
                content = bf.read()
                
            # Compress content deterministically by setting mtime=0
            compressed = bytearray(gzip.compress(content, mtime=0))
            # Force OS byte (10th byte) to 255 (unknown) to avoid platform/python differences
            if len(compressed) >= 10:
                compressed[9] = 255
            
            rel_path = os.path.relpath(filepath, data_dir).replace("\\", "/")
            var_name = "web_asset_" + rel_path.replace("/", "_").replace(".", "_").replace("-", "_")
            
            f.write(f"const uint8_t {var_name}[] PROGMEM = {{\n")
            for i in range(0, len(compressed), 16):
                chunk = compressed[i:i+16]
                f.write("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
            f.write("};\n")
            f.write(f"const size_t {var_name}_len = {len(compressed)};\n\n")

        f.write("#endif // WEB_ASSETS_H\n")
        
    with open(hash_file, "w") as hf:
        hf.write(current_hash)

embed_files()
