FROM ubuntu:latest

WORKDIR /workdir

RUN apt-get update && cd build && \
    apt-get install -y ./leap*.deb -y && \
    rm leap_4.0.4-ubuntu22.04_amd64.deb
