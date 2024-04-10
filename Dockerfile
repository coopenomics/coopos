FROM ubuntu:latest

WORKDIR /workdir
COPY build/leap*.deb /workdir/build/

RUN apt-get update && cd build && \
    apt-get install -y ./leap*.deb -y && \
    rm leap*.deb

