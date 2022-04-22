FROM ubuntu:focal

RUN export DEBIAN_FRONTEND=noninteractive \
    && apt-get update \
    && apt-get install -yq software-properties-common \
    && add-apt-repository -y ppa:ubuntu-toolchain-r/test \
    && apt-get update \
    && apt-get install -yq      \
        autoconf                \
        binaryen                \
        build-essential         \
        cmake                   \
        curl                    \
        g++-11                  \
        gcc-11                  \
        git                     \
        libcurl4-openssl-dev    \
        libgbm-dev              \
        libgmp-dev              \
        libnss3-dev             \
        libssl-dev              \
        libstdc++-11-dev        \
        libtool                 \
        libusb-1.0-0-dev        \
        libzstd-dev             \
        pkg-config              \
        zstd                    \
    && apt-get clean -yq \
    && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100 \
    && update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-11 100 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-11 100

RUN cd /root \
    && curl -LO https://github.com/ccache/ccache/releases/download/v4.3/ccache-4.3.tar.gz \
    && tar xf ccache-4.3.tar.gz \
    && cd /root/ccache-4.3 \
    && cmake . \
    && make -j \
    && make -j install \
    && cd /root \
    && rm -rf ccache*

RUN cd /root \
    && curl -LO https://boostorg.jfrog.io/artifactory/main/release/1.78.0/source/boost_1_78_0.tar.gz \
    && tar xf boost_1_78_0.tar.gz \
    && cd boost_1_78_0 \
    && ./bootstrap.sh \
    && ./b2 --prefix=/usr/local --build-dir=build variant=release --with-chrono --with-date_time \
            --with-filesystem --with-iostreams --with-program_options --with-system --with-test install \
    && cd /root \
    && rm -rf boost*

RUN cd /opt \
    && curl -LO https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-14/wasi-sdk-14.0-linux.tar.gz \
    && tar xf wasi-sdk-14.0-linux.tar.gz \
    && curl -LO https://nodejs.org/dist/v14.16.0/node-v14.16.0-linux-x64.tar.xz \
    && tar xf node-v14.16.0-linux-x64.tar.xz \
    && rm *.tar.* \
    && export PATH="/opt/node-v14.16.0-linux-x64/bin:$PATH" \
    && npm i -g yarn

RUN cd /root \
    && curl --proto '=https' --tlsv1.2 -sSf -o rustup.sh https://sh.rustup.rs \
    && chmod 700 rustup.sh \
    && ./rustup.sh -y --no-modify-path \
    && /root/.cargo/bin/cargo install mdbook \
    && /root/.cargo/bin/cargo install sccache \
    && rm rustup.sh

RUN chmod o+rx /root

ENV WASI_SDK_PREFIX=/opt/wasi-sdk-14.0
ENV PATH=/root/.cargo/bin:/opt/node-v14.16.0-linux-x64/bin:$PATH
