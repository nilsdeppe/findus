ARG UBUNTU_VERSION=24.04
FROM ubuntu:${UBUNTU_VERSION} AS base
ARG UBUNTU_VERSION=24.04

RUN apt-get update -y
RUN apt-get install -y doxygen hwloc libhwloc-dev ccache
RUN apt-get install -y gcc g++ cmake git libopenmpi-dev
RUN apt-get install -y autoconf automake clang-format
RUN apt-get update -y
RUN apt-get install -y gdb clang++-16
