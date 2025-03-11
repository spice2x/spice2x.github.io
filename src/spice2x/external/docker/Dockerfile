FROM archlinux:base

RUN pacman --noconfirm -Syu gettext \
    base-devel \
    git \
    bash \
    zip \
    upx \
    sudo \
    binutils \
    file \
    make \
    gcc \
    fakeroot \
    diffutils \
    cmake \
    awk \
    unzip \
    mingw-w64-crt \
    mingw-w64-winpthreads \
    mingw-w64-gcc \
    mingw-w64-headers \
    mingw-w64-binutils
RUN useradd user -m && echo "ALL ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
RUN su user -c "cd /tmp/ && git clone https://aur.archlinux.org/yay.git --depth=1 && cd yay && makepkg --noconfirm -si && yay --noconfirm -S mingw-w64-cmake"
