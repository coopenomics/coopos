FROM ubuntu:latest

WORKDIR /workdir

RUN apt-get update && apt-get install -y wget && \
    apt-get install -y ./leap*.deb -y && \
    rm leap_4.0.4-ubuntu22.04_amd64.deb

