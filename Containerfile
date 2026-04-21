FROM docker.io/library/ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libncursesw5-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workdir
