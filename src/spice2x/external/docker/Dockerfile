FROM ghcr.io/archlinux/archlinux:base-devel

RUN pacman --noconfirm -Syu git \
    bash \
    zip \
    upx \
    ninja \
    cmake \
    unzip \
    nasm \
    mingw-w64-crt \
    mingw-w64-winpthreads \
    mingw-w64-gcc \
    mingw-w64-headers \
    mingw-w64-binutils \
    ccache
RUN useradd user -m && echo "ALL ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
RUN su user -c "cd /tmp/ && git clone https://aur.archlinux.org/yay-bin.git --depth=1 && cd yay-bin && makepkg --noconfirm -si && yay --noconfirm -S mingw-w64-cmake"

RUN mkdir -p /opt/llvm-mingw-xp \
    && curl -fsSL "https://github.com/mon/llvm-mingw-xp/releases/download/llvm-mingw-xp-22.1.0/llvm-mingw-llvm-mingw-xp-22.1.0-msvcrt-ubuntu-22.04-x86_64.tar.xz" \
    | tar -xJ --strip-components=1 -C /opt/llvm-mingw-xp
ENV PATH="$PATH:/opt/llvm-mingw-xp/bin"

RUN curl -fsSL "https://github.com/mon/windows-dll-compat-checker/releases/download/v1.3/windows_dll_compat_checker-linux-x86_64.tar.xz" \
    | tar -xJ -C /usr/local/bin

# Stock makepkg.conf builds serially; this makes the AUR compiles below parallel.
RUN printf '%s\n' 'MAKEFLAGS="-j$(nproc)"' > /home/user/.makepkg.conf \
    && chown user: /home/user/.makepkg.conf

# libjpeg-turbo for JPEG encoding, x264 for the API H.264 video stream. Only the
# mingw-w64 toolchains get these; the WinXP targets build without JPEG support
# and without the stream encoder.
RUN su user -c "yay --noconfirm -S mingw-w64-libjpeg-turbo mingw-w64-x264"
