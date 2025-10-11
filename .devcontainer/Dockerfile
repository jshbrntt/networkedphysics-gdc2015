FROM mcr.microsoft.com/devcontainers/base:debian-12 AS base

RUN wget https://github.com/premake/premake-core/releases/download/v5.0.0-beta1/premake-5.0.0-beta1-linux.tar.gz \
&& tar -xzf premake-5.0.0-beta1-linux.tar.gz -C /usr/local/bin

RUN echo "alias pm='premake5'" >> /home/vscode/.bashrc

RUN apt-get update && apt-get install --no-install-recommends --yes \
autoconf \
automake \
clang-19 \
libfreetype-dev \
libglew-dev \
libglfw3-dev \
libglm-dev \
libjansson-dev \
libpng-dev \
libtool \
libxi-dev \
libxrandr-dev \
libxxf86vm-dev \
nodejs \
tig

ENV CC="clang-19"
ENV CXX="clang++-19"
