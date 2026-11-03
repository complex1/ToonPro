#!/usr/bin/env bash
# Build a private FFmpeg/OpenH264 runtime without changing the Qt SDK.
# Usage: build_linux_codecs.sh FRESH_OUTPUT_DIRECTORY [READ_ONLY_SOURCE_CACHE]
set -euo pipefail
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo 'This recipe requires Linux x86_64 (Ubuntu 24.04 baseline).' >&2
    exit 2
fi
TOON_CODEC_JOBS=${TOON_CODEC_JOBS:-1}
if [[ "$TOON_CODEC_JOBS" != 1 && "$TOON_CODEC_JOBS" != 2 ]]; then
    echo 'TOON_CODEC_JOBS must be 1 or 2.' >&2
    exit 2
fi
for command in cc c++ make nasm pkg-config python3 tar; do
    command -v "$command" >/dev/null || { echo "Missing dependency: $command" >&2; exit 2; }
done
TOON_CODEC_OUTPUT=$(realpath -m "${1:?Supply a fresh output directory}")
TOON_CODEC_CACHE=${2:-}
if [[ -e "$TOON_CODEC_OUTPUT" ]] && [[ -n $(find "$TOON_CODEC_OUTPUT" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
    echo 'Output must be new or empty; existing builds are preserved.' >&2
    exit 2
fi
mkdir -p "$TOON_CODEC_OUTPUT"/{sources,logs,install,dependency-sources}
TOON_CODEC_PREFIX="$TOON_CODEC_OUTPUT/install"
TOON_CODEC_SCRIPT=$(realpath "$0")
export TOON_CODEC_OUTPUT TOON_CODEC_CACHE TOON_CODEC_PREFIX TOON_CODEC_SCRIPT
python3 - <<'PY'
import hashlib, os, pathlib, shutil, tarfile, urllib.request
root=pathlib.Path(os.environ['TOON_CODEC_OUTPUT'])
cache=pathlib.Path(os.environ['TOON_CODEC_CACHE']) if os.environ['TOON_CODEC_CACHE'] else None
sources=[('FFmpeg-n7.1.3.tar.gz','https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n7.1.3','e0b04c4b43d7e6d67cb6710334fb513adf13ac860532f30e1d0ac4c231f232fb'),('openh264-2.7.0.tar.gz','https://codeload.github.com/cisco/openh264/tar.gz/refs/tags/v2.7.0','5178775c8b440073e70920e97d195efff721c0e56d0a78cc82c919e95d54924d')]
for name,url,expected in sources:
    destination=root/'dependency-sources'/name
    if cache and (cache/name).is_file(): shutil.copyfile(cache/name,destination)
    else:
        with urllib.request.urlopen(url,timeout=60) as response,destination.open('wb') as output:
            size=0
            while block:=response.read(1024*1024):
                size+=len(block)
                if size>256*1024*1024: raise RuntimeError('Source archive exceeds limit')
                output.write(block)
    if hashlib.sha256(destination.read_bytes()).hexdigest()!=expected: raise RuntimeError('Source checksum mismatch: '+name)
    with tarfile.open(destination,'r:gz') as archive: archive.extractall(root/'sources',filter='data')
shutil.copyfile(os.environ['TOON_CODEC_SCRIPT'],root/'dependency-sources/build_linux_codecs.sh')
PY
echo 'Building OpenH264 2.7.0 (one shared library).'
make -C "$TOON_CODEC_OUTPUT/sources/openh264-2.7.0" -j"$TOON_CODEC_JOBS" \
    OS=linux ARCH=x86_64 BUILDTYPE=Release PREFIX="$TOON_CODEC_PREFIX" \
    install-shared >"$TOON_CODEC_OUTPUT/logs/openh264-build.log" 2>&1
mkdir "$TOON_CODEC_OUTPUT/ffmpeg-build"
cd "$TOON_CODEC_OUTPUT/ffmpeg-build"
export PKG_CONFIG_PATH="$TOON_CODEC_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
# Preserve Qt's FFmpeg 7.1.3 configuration and add software H.264. GPL,
# nonfree and version3 stay explicitly disabled; no libx264/libfdk_aac.
TOON_FFMPEG_OPTIONS=(--disable-programs --disable-doc --disable-debug
    --enable-network --disable-lzma --enable-pic --disable-vulkan
    --disable-v4l2-m2m --disable-decoder=truemotion1 --enable-shared
    --disable-static --enable-openssl --enable-libopenh264 --cpu=x86-64
    --disable-gpl --disable-nonfree --disable-version3 --prefix="$TOON_CODEC_PREFIX")
printf '%s\n' "${TOON_FFMPEG_OPTIONS[@]}" >"$TOON_CODEC_OUTPUT/dependency-sources/ffmpeg-configure-options.txt"
echo 'Configuring FFmpeg 7.1.3 with OpenH264 software encoding.'
"$TOON_CODEC_OUTPUT/sources/FFmpeg-n7.1.3/configure" "${TOON_FFMPEG_OPTIONS[@]}" \
    >"$TOON_CODEC_OUTPUT/logs/ffmpeg-configure.log" 2>&1
echo 'Building FFmpeg shared libraries; full codec coverage, bounded jobs.'
make -j"$TOON_CODEC_JOBS" >"$TOON_CODEC_OUTPUT/logs/ffmpeg-build.log" 2>&1
make install >"$TOON_CODEC_OUTPUT/logs/ffmpeg-install.log" 2>&1
cp config.h config_components.h ffbuild/config.mak "$TOON_CODEC_OUTPUT/dependency-sources/"
mkdir -p "$TOON_CODEC_PREFIX/third-party/licenses"
cp "$TOON_CODEC_OUTPUT/sources/FFmpeg-n7.1.3/COPYING.LGPLv2.1" "$TOON_CODEC_PREFIX/third-party/licenses/FFmpeg-LGPL-2.1-or-later.txt"
cp "$TOON_CODEC_OUTPUT/sources/FFmpeg-n7.1.3/LICENSE.md" "$TOON_CODEC_PREFIX/third-party/licenses/FFmpeg-component-notices.txt"
cp "$TOON_CODEC_OUTPUT/sources/openh264-2.7.0/LICENSE" "$TOON_CODEC_PREFIX/third-party/licenses/OpenH264-BSD-2-Clause.txt"
LD_LIBRARY_PATH="$TOON_CODEC_PREFIX/lib" python3 - <<'PY'
import ctypes,hashlib,json,os,pathlib,platform,shutil,subprocess
root=pathlib.Path(os.environ['TOON_CODEC_OUTPUT']);prefix=root/'install'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
versions={}
for module,major in [('avcodec',61),('avformat',61),('avutil',59),('swresample',5),('swscale',8)]:
    library=ctypes.CDLL(str(prefix/'lib'/f'lib{module}.so.{major}'))
    fn=getattr(library,module+'_version');fn.restype=ctypes.c_uint
    version=fn();assert version>>16==major,(module,version)
    versions[module]=version
codec=ctypes.CDLL(str(prefix/'lib/libavcodec.so.61'))
codec.avcodec_find_encoder_by_name.argtypes=[ctypes.c_char_p];codec.avcodec_find_encoder_by_name.restype=ctypes.c_void_p
assert codec.avcodec_find_encoder_by_name(b'libopenh264'),'Missing OpenH264 encoder'
assert codec.avcodec_find_encoder_by_name(b'aac'),'Missing native AAC encoder'
codec.avcodec_find_decoder_by_name.argtypes=[ctypes.c_char_p];codec.avcodec_find_decoder_by_name.restype=ctypes.c_void_p
assert codec.avcodec_find_decoder_by_name(b'h264') and codec.avcodec_find_decoder_by_name(b'aac')
codec.avcodec_license.restype=ctypes.c_char_p;license=codec.avcodec_license().decode()
assert license=='LGPL version 2.1 or later',license
codec.avcodec_configuration.restype=ctypes.c_char_p;configuration=codec.avcodec_configuration().decode()
assert '--enable-gpl' not in configuration and '--enable-nonfree' not in configuration
sources=[{'name':'FFmpeg','version':'7.1.3','archive':'FFmpeg-n7.1.3.tar.gz','url':'https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.3.tar.gz','license':'LGPL-2.1-or-later'},{'name':'OpenH264','version':'2.7.0','archive':'openh264-2.7.0.tar.gz','url':'https://github.com/cisco/openh264/archive/refs/tags/v2.7.0.tar.gz','license':'BSD-2-Clause'}]
for source in sources:source['sha256']=sha(root/'dependency-sources'/source['archive'])
manifest={'schemaVersion':1,'platform':'Linux','architecture':platform.machine(),'ffmpegVersion':'7.1.3','openh264Version':'2.7.0','license':license,'sources':sources,'configuration':configuration,'abiVersions':versions,'sourceModified':False,'encoderDiscoveryVerified':True,'qtEndToEndVerified':False,'legalApproval':False,'libraries':{p.name:sha(p) for p in sorted((prefix/'lib').glob('*.so*')) if p.is_file() and not p.is_symlink()},'compiler':subprocess.check_output(['cc','--version'],text=True).splitlines()[0]}
(prefix/'third-party/sources.json').write_text(json.dumps(manifest,indent=2)+'\n')
(prefix/'third-party/THIRD-PARTY-NOTICES.txt').write_text('Toon Pro Linux software video runtime\n\nFFmpeg 7.1.3: LGPL-2.1-or-later. OpenH264 2.7.0: BSD-2-Clause.\nNo GPL, nonfree or version3 FFmpeg components enabled. Source files are unmodified;\nconfigure options differ from the SDK by adding software OpenH264 encoding.\nThis software is based in part on the work of the Independent JPEG Group.\nFull licenses and component notices are in licenses/. Rebuild inputs, source\narchives and checksums accompany this release; see sources.json.\n\nThis OpenH264 library is built from source and distributed by Toon Pro, not\ndownloaded by the user from Cisco. Cisco\'s binary patent royalty coverage does\nnot apply to this build. Codec patent/distribution review remains a release\nrequirement. See https://www.openh264.org/faq.html and the source LICENSE.\n\nOpenSSL and other system libraries remain supplied by the operating system.\nThis build recipe does not choose the license of Toon Pro application code.\n')
shutil.copytree(prefix/'third-party',root/'dependency-sources/third-party')
(root/'dependency-sources/SHA256SUMS').write_text(''.join(f'{sha(p)}  {p.relative_to(root/"dependency-sources").as_posix()}\n' for p in sorted((root/'dependency-sources').rglob('*')) if p.is_file()))
print('Verified shared ABI, H.264/AAC encoder/decoder discovery and LGPL configuration.')
print(prefix)
PY
