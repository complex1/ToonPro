#!/usr/bin/env python3
"""Prepare the optional local RIFE v4.6 runtime; never called by CMake."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
RELEASE = '20221029'
MAC_SHA256 = '4a63a1f3c9c715773c57d2ee51df1b315ed20cd6c63103e45c483ecc4400b595'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--from-existing', type=Path, help='Reuse an extracted official runtime')
    parser.add_argument('--destination', type=Path, default=ROOT / '.deps/rife')
    args = parser.parse_args()
    system = {'Darwin': 'macos', 'Windows': 'windows', 'Linux': 'ubuntu'}.get(platform.system())
    if not system:
        parser.error('No runtime is configured for this operating system')
    executable = 'rife-ncnn-vulkan' + ('.exe' if system == 'windows' else '')
    files = [executable, 'LICENSE', 'README.md', 'rife-v4.6/flownet.bin', 'rife-v4.6/flownet.param']
    url = f'https://github.com/nihui/rife-ncnn-vulkan/releases/download/{RELEASE}/rife-ncnn-vulkan-{RELEASE}-{system}.zip'
    with tempfile.TemporaryDirectory(prefix='toon-rife-') as temporary:
        staging = Path(temporary)
        provenance = {'release': RELEASE, 'model': 'rife-v4.6', 'url': url}
        if args.from_existing:
            source = args.from_existing.resolve()
            provenance['reused_extracted_runtime'] = str(source)
        else:
            archive = staging / 'runtime.zip'
            print('Downloading the official runtime archive (includes additional models)…', flush=True)
            urllib.request.urlretrieve(url, archive)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            provenance['archive_sha256'] = digest
            if system == 'macos' and digest != MAC_SHA256:
                raise SystemExit('The runtime archive checksum does not match the pinned release')
            source = staging / f'rife-ncnn-vulkan-{RELEASE}-{system}'
            with zipfile.ZipFile(archive) as bundle:
                for name in files:
                    bundle.extract(f'{source.name}/{name}', staging)
        for name in files:
            if not (source / name).is_file():
                raise SystemExit(f'Missing runtime file: {source / name}')
        # Copy only the model actually used, plus its executable and notices.
        destination = args.destination.resolve()
        destination.mkdir(parents=True, exist_ok=True)
        provenance['files_sha256'] = {}
        for name in files:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / name, target)
            provenance['files_sha256'][name] = hashlib.sha256(target.read_bytes()).hexdigest()
        if system != 'windows':
            (destination / executable).chmod(0o755)
        (destination / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        print(f'Prepared {destination}. Reconfigure and rebuild ToonPro to bundle it.')


if __name__ == '__main__':
    main()
