import urllib.request
import json
import zipfile
import tarfile
import os
import io

API_URL = "https://components.espressif.com/api/components/espressif/usb_host_uvc"

print("Fetching metadata...")
req = urllib.request.Request(API_URL)
with urllib.request.urlopen(req) as response:
    data = json.loads(response.read().decode())
    
versions = data.get("versions", [])
target_version = None
for v in versions:
    if v.get("version") == "2.6.0":
        target_version = v
        break

if not target_version:
    print("Version 2.6.0 not found!")
    exit(1)

download_url = target_version.get("url")
print(f"Downloading from {download_url}...")

with urllib.request.urlopen(download_url) as response:
    archive_data = response.read()

print("Extracting...")
os.makedirs("components/espressif__usb_host_uvc", exist_ok=True)
with zipfile.ZipFile(io.BytesIO(archive_data)) as zip_ref:
    zip_ref.extractall("components/espressif__usb_host_uvc")

print("Done!")
