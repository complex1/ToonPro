# Build on the same glibc baseline as the official Qt 6.10 Linux SDK.
# Run with --platform linux/amd64 when the host is Apple Silicon.
# Source can be mounted read-only at /source; put builds/packages in /out.
FROM ubuntu:24.04
ARG QT_VERSION=6.10.3
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl build-essential cmake ninja-build python3 python3-venv \
    zlib1g-dev pkg-config nasm libssl-dev libva-dev libvdpau-dev \
    libva2 libva-drm2 libva-x11-2 libvdpau1 \
    libgtk-3-0t64 libwayland-cursor0 libwayland-egl1 libxcb-shape0 \
    patchelf binutils xvfb xauth fontconfig fonts-dejavu-core \
    libgl1-mesa-dev libegl1-mesa-dev libgl1-mesa-dri libfontconfig1 libfreetype6 \
    libx11-xcb1 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
    libxcb-render-util0 libxcb-xinerama0 libxcb-xkb1 libxcb-xinput0 \
    libxkbcommon-x11-0 libdbus-1-3 libasound2t64 libpulse0 libnss3 libglib2.0-0t64 \
    libxcomposite1 libxrandr2 libxdamage1 libxss1 libxtst6 \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt \
    && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0 \
    && /opt/aqt/bin/python -m aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 \
         --outputdir /opt/Qt --modules qtmultimedia --archives qtbase qtsvg qtmultimedia qtdeclarative icu
ENV LANG=C.UTF-8 \
    PATH=/opt/aqt/bin:/opt/Qt/${QT_VERSION}/gcc_64/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    QT_QPA_OFFSCREEN_NO_GLX=1
WORKDIR /out
# No tests or publishing run merely by starting the image.
CMD ["/bin/bash"]
