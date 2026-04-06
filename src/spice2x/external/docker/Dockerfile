FROM ghcr.io/archlinux/archlinux:base-devel

RUN pacman --noconfirm -Syu git \
    bash \
    zip \
    upx \
    ninja \
    cmake \
    unzip \
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
