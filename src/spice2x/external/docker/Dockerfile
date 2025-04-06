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
    mingw-w64-binutils
RUN useradd user -m && echo "ALL ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
RUN su user -c "cd /tmp/ && git clone https://aur.archlinux.org/yay-bin.git --depth=1 && cd yay-bin && makepkg --noconfirm -si && yay --noconfirm -S mingw-w64-cmake"
