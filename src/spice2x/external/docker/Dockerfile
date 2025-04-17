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

ENV CCACHE_DIR=/ccache
ENV CCACHE_MAXSIZE=10G
RUN mkdir -p /ccache && chmod 777 /ccache

ENV CMAKE_CXX_COMPILER_LAUNCHER=ccache
ENV CMAKE_C_COMPILER_LAUNCHER=ccache