FROM openorbisofficial/toolchain@sha256:f752d09f9d3227c729ee069f29a05450bd8fa4ef5e785c0929ab8479f7dc4e64

USER root
RUN apt-get update && apt-get install -y --no-install-recommends llvm-10 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ADD https://github.com/illusion0001/OpenOrbis-PS4-Toolchain/releases/latest/download/toolchain.tar.gz /tmp/toolchain.tar.gz
RUN mkdir -p /opt/ps4 \
    && tar -xzf /tmp/toolchain.tar.gz -C /opt/ps4 --strip-components=2 \
    && rm /tmp/toolchain.tar.gz
ENV OO_PS4_TOOLCHAIN=/opt/ps4

ARG GOLDHEN_COMMIT=bcea3c7ef01dac6d7f9f49ebf9e90fe66d86f5f7
ADD https://codeload.github.com/GoldHEN/GoldHEN_Plugins_SDK/tar.gz/${GOLDHEN_COMMIT} /tmp/goldhen.tar.gz
RUN mkdir -p /tmp/goldhen \
    && tar -xzf /tmp/goldhen.tar.gz -C /tmp/goldhen --strip-components=1 \
    && make -C /tmp/goldhen install AR=llvm-ar-10 \
    && cp /tmp/goldhen/build/crtprx.o /opt/ps4/lib/crtprx.o \
    && rm -rf /tmp/goldhen /tmp/goldhen.tar.gz

WORKDIR /workspace
CMD ["make", "INTDIR=build/ps4", "all", "test"]
