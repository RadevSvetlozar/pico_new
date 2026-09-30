Import("env")

import hashlib
import json
import re
import shutil
from pathlib import Path


def export_firmware(target, source, env):
    if env.subst("$PIOENV") != "arduino_nano_esp32_ota":
        return

    project_dir = Path(env.subst("$PROJECT_DIR"))
    source_binary = Path(str(target[0]))
    exported_binary = project_dir / "firmware.bin"
    shutil.copyfile(source_binary, exported_binary)

    app_source = (project_dir / "src" / "App.cpp").read_text(encoding="utf-8")
    version_match = re.search(r'firmwareVersion\s*=\s*"([^"]+)"', app_source)
    if not version_match:
        raise RuntimeError("firmwareVersion was not found in src/App.cpp")

    digest = hashlib.sha256(exported_binary.read_bytes()).hexdigest()
    release = {
        "board": "arduino_nano_esp32",
        "version": version_match.group(1),
        "file": "firmware.bin",
        "download_url": "https://raw.githubusercontent.com/RadevSvetlozar/pico_new/main/firmware.bin",
        "sha256": digest,
        "file_size_bytes": exported_binary.stat().st_size,
    }
    (project_dir / "firmware-release.json").write_text(
        json.dumps(release, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Exported firmware {release['version']} ({digest})")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", export_firmware)
