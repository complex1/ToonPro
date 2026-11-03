# Add runtime dependencies required by FFmpeg and the complete deployed Qt set.
# Reuse the existing Qt/application build image; no Qt download or rebuild.
ARG TOON_RELEASE_BASE=toon-pro-release-075-linux
FROM ${TOON_RELEASE_BASE}
USER root
RUN apt-get update && apt-get install -y --no-install-recommends \
    libva2 libva-drm2 libva-x11-2 libvdpau1 \
    libgtk-3-0t64 libwayland-cursor0 libwayland-egl1 libxcb-shape0 \
    && rm -rf /var/lib/apt/lists/*
