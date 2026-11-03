# Clean Ubuntu runtime for relocated Toon Pro package checks.
# No Qt SDK, application source, compiler, CMake, make or codec build tools.
# Run the x86_64 package with --platform linux/amd64 on Apple Silicon.
# System libraries below were mapped from all 84 deployed ELF files; package
# dependencies supply GTK/Wayland/XCB/media runtimes without a development SDK.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates python3 binutils xvfb xauth fonts-dejavu-core fontconfig \
    libasound2t64 libgl1-mesa-dri libegl-mesa0 libglx-mesa0 \
    libapparmor1 libasyncns0 libatk-bridge2.0-0t64 libatk1.0-0t64 libatspi2.0-0t64 libblkid1 \
    libbrotli1 libbsd0 libbz2-1.0 libc6 libcairo-gobject2 libcairo2 \
    libcap2 libcom-err2 libdatrie1 libdbus-1-3 libdrm2 libegl1 \
    libepoxy0 libexpat1 libffi8 libflac12t64 libfontconfig1 libfreetype6 \
    libfribidi0 libgbm1 libgcc-s1 libgcrypt20 libgdk-pixbuf-2.0-0 libgl1 \
    libglib2.0-0t64 libglvnd0 libglx0 libgpg-error0 libgraphite2-3 libgssapi-krb5-2 \
    libgtk-3-0t64 libharfbuzz0b libjpeg-turbo8 libk5crypto3 libkeyutils1 libkrb5-3 \
    libkrb5support0 liblz4-1 liblzma5 libmd0 libmount1 libmp3lame0 \
    libmpg123-0t64 libogg0 libopengl0 libopus0 libpango-1.0-0 libpangocairo-1.0-0 \
    libpangoft2-1.0-0 libpcre2-8-0 libpixman-1-0 libpng16-16t64 libpulse0 libselinux1 \
    libsndfile1 libssl3t64 libstdc++6 libsystemd0 libthai0 libva-drm2 \
    libva-x11-2 libva2 libvdpau1 libvorbis0a libvorbisenc2 libwayland-client0 \
    libwayland-cursor0 libwayland-egl1 libx11-6 libx11-xcb1 libxau6 libxcb-cursor0 \
    libxcb-dri3-0 libxcb-glx0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
    libxcb-render-util0 libxcb-render0 libxcb-shape0 libxcb-shm0 libxcb-sync1 libxcb-util1 \
    libxcb-xfixes0 libxcb-xkb1 libxcb1 libxcomposite1 libxcursor1 libxdamage1 \
    libxdmcp6 libxext6 libxfixes3 libxi6 libxinerama1 libxkbcommon-x11-0 \
    libxkbcommon0 libxrandr2 libxrender1 libzstd1 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && ! command -v gcc && ! command -v g++ && ! command -v cmake \
    && ! command -v make && ! command -v qmake && test ! -e /opt/Qt
ENV LANG=C.UTF-8
WORKDIR /work
# Starting the image does not install, run or publish the application.
# Mount only the finished archive and verifier; extract to an isolated directory.
CMD ["/bin/bash"]
