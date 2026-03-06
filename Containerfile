FROM docker.io/library/archlinux:latest

# Initialize keys and install dependencies
RUN pacman-key --init && \
    pacman-key --populate archlinux && \
    pacman -Syu --noconfirm && \
    pacman -S --noconfirm gcc ncurses && \
    pacman -Scc --noconfirm

WORKDIR /workdir
