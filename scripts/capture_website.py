#!/usr/bin/env python3
"""Recapture real Toon Pro UI fixtures and publish compact, attributed WebP images."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--output', type=Path, default=ROOT / 'website/assets/screenshots/current')
    parser.add_argument('--work-dir', type=Path, default=ROOT / 'build/website-captures')
    parser.add_argument('--build', action='store_true', help='Build required capture binaries first')
    args = parser.parse_args()
    build, output, work = (p.resolve() for p in (args.build_dir, args.output, args.work_dir))
    encoder = shutil.which('cwebp')
    if not encoder:
        parser.error('cwebp is required for lossless screenshot conversion (install libwebp tools)')
    jobs = [
        ('dashboard', 'managed_project', ['dashboardGalleriesFollowProjectTypeAndRenderArtwork', 'realCreationDialogsAndIndependentAnimationTabs', 'tabsRetainIndependentArtworkHistoryAndView'], 'TOON_MANAGED_CAPTURE'),
        ('composition', 'ux_capture', ['--drawing-composition'], None),
        ('density', 'ux_capture', ['--drawing-density'], None),
        ('inspectors', 'ux_capture', ['--drawing-inspectors'], None),
        ('advanced', 'ux_capture', ['--advanced-management'], None),
        ('reference3d', 'experimental_3d', ['flagExposesOnlyTheNewReferenceWorkspace'], 'TOON_REFERENCE3D_APP_CAPTURE_DIR'),
        ('fill', 'fill_reference_ui', ['captureReviewedControls'], 'TOON_FILL_REFERENCE_CAPTURE_DIR'),
        ('perspective', 'perspective_dialog', ['captureReviewedDetection'], 'TOON_PERSPECTIVE_CAPTURE_DIR'),
        ('puppet', 'puppet_warp_ui', ['captureReviewedWorkflow'], 'TOON_PUPPET_CAPTURE_DIR'),
        ('outline', 'layer_outline', ['captureReviewedOutline'], 'TOON_OUTLINE_CAPTURE_DIR'),
        ('conversion', 'line_art', ['captureReviewedWorkflows'], 'TOON_LINE_ART_CAPTURE_DIR'),
    ]
    targets = sorted({f'toon_{target}' if target == 'ux_capture' else f'toon_{target}_tests' for _, target, _, _ in jobs})
    if args.build:
        subprocess.run(['cmake', '--build', str(build), '--parallel', '3', '--target', *targets], cwd=ROOT, check=True)
    work.mkdir(parents=True, exist_ok=True)
    version = re.search(r'project\(ToonPro VERSION ([0-9.]+)', (ROOT/'CMakeLists.txt').read_text()).group(1)
    env = {**os.environ, 'QT_SCALE_FACTOR': '1', 'TOON_CAPTURE_APP_VERSION': version}
    for name, target, slots, variable in jobs:
        folder = work / name
        folder.mkdir(exist_ok=True)
        executable = build / (f'toon_{target}' if target == 'ux_capture' else f'toon_{target}_tests')
        command = [str(executable)]
        if variable:
            job_env = {**env, variable: str(folder)}
            command += slots
        else:
            job_env = env
            command += [str(folder), *slots]
        command += ['-platform', 'offscreen']
        print(f'Capturing {name}…', flush=True)
        with (work / f'{name}.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, env=job_env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)

    def captured(job: str, family: str, state: str | None = None) -> Path:
        manifest = json.loads((work / job / 'capture-manifest.json').read_text())
        if manifest['capture_notes']:
            raise RuntimeError(f'{job} capture reported: {manifest["capture_notes"]}')
        for screen in manifest['screens']:
            if screen['family'] == family and (state is None or screen['state'] == state):
                return work / job / screen['file']
        raise RuntimeError(f'Missing capture: {job}: {family}: {state}')

    imported = 'Real app capture with imported sample illustration; the illustration was not created in Toon Pro.'
    sample = 'Real app capture using a temporary synthetic sample document.'
    images = [
        ('projects-home', work/'dashboard/projects-home.png', 'Projects dashboard with sample drawing, animation and movie projects.', imported),
        ('new-project', work/'dashboard/new-project-cards.png', 'Create a project: drawing, animation or movie.', sample),
        ('drawing-setup', work/'dashboard/new-drawing-movie.png', 'Drawing setup with canvas presets, custom dimensions and background preview.', sample),
        ('drawing-gallery', work/'dashboard/drawing-gallery.png', 'Drawing project gallery with independent documents.', imported),
        ('assets-gallery', work/'dashboard/assets-gallery.png', 'Project-owned assets with folders, tags and media previews.', imported),
        ('editor-tabs', work/'dashboard/editor-tabs.png', 'Independent document tabs in the managed workspace.', sample),
        ('animation-workspace', work/'dashboard/animation-tab.png', 'Single-shot animation document open in the managed workspace.', sample),
        ('drawing-workspace', captured('composition', 'Workspace', 'Drawing preset — daily controls adjacent'), 'Drawing workspace with direct tool controls, canvas and layers.', sample),
        ('layers', captured('density', 'Layers', 'Standard drawing dock'), 'Layers dock with visibility, opacity and layer actions.', sample),
        ('brush-settings', captured('advanced', 'Advanced Brush', 'General'), 'Advanced brush settings: General category.', sample),
        ('brush-dynamics', captured('advanced', 'Advanced Brush', 'Shape dynamics'), 'Shape dynamics with editable input-response controls.', sample),
        ('guides', captured('inspectors', 'Guides', 'Multiple assistants'), 'Guide inspector with multiple editable assistants.', sample),
        ('selection', captured('inspectors', 'Selection and Transform', 'Selection'), 'Selection inspector and selection modes.', sample),
        ('vector', captured('inspectors', 'Vector Properties', 'Tool 22'), 'Vector editing inspector with an editable brush path selected.', sample),
        ('reference-3d', work/'reference3d/main-app-toolbar.png', 'Native 3D reference layer with an anime model and dog in the drawing canvas.', sample),
        ('fill-reference', work/'fill/fill-reference-properties.png', 'Fill tool using marked reference layers as boundaries.', sample),
        ('perspective-detection', work/'perspective/perspective-detection-dialog.png', 'Review detected perspective directions before adding editable guides.', sample),
        ('puppet-warp', work/'puppet/puppet-workspace.png', 'Puppet Warp with mesh pins in a temporary raster fixture.', sample),
        ('layer-outline', work/'outline/outline-controls.png', 'Live layer-outline controls.', sample),
        ('line-conversion', work/'conversion/image-conversion.png', 'Raster line and tone conversion preview using imported sample artwork.', imported),
    ]
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    for slug, source, caption, provenance in images:
        with source.open('rb') as f:
            header = f.read(24)
        if header[:8] != b'\x89PNG\r\n\x1a\n':
            raise RuntimeError(f'Capture is not a PNG: {source}')
        width, height = struct.unpack('>II', header[16:24])
        destination = output/f'{slug}.webp'
        subprocess.run([encoder, '-quiet', '-lossless', '-z', '6', str(source), '-o', str(destination)], check=True)
        entries.append({'slug':slug, 'file':destination.name, 'sha256':hashlib.sha256(destination.read_bytes()).hexdigest(), 'width':width, 'height':height, 'caption':caption, 'provenance':provenance, 'source':str(source.relative_to(work))})
    manifest = {'application':'Toon Pro', 'version':version, 'capturedAt':datetime.now(timezone.utc).isoformat(), 'platform':'Qt offscreen, scale factor 1', 'captureCommand':'python3 scripts/capture_website.py --build', 'method':'Fresh captures of real application widgets. Temporary fixture documents; no mockups.', 'screenshots':entries}
    encoded = json.dumps(manifest, indent=2)+'\n'
    (output/'manifest.json').write_text(encoded)
    (output/'index.json').write_text(encoded)
    print(f'Wrote {len(entries)} lossless WebP screenshots and {output}/manifest.json')


if __name__ == '__main__':
    main()
