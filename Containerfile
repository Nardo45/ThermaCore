FROM docker.io/library/archlinux:latest

RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm gcc ncurses && \
    pacman -Scc --noconfirm

WORKDIR /workdir
