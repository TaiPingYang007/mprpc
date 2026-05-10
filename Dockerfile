FROM ubuntu:24.04 AS muduo-builder

ARG DEBIAN_FRONTEND=noninteractive
ARG MUDUO_REF=b55b0ac
ENV http_proxy= \
    https_proxy= \
    HTTP_PROXY= \
    HTTPS_PROXY= \
    ALL_PROXY= \
    all_proxy= \
    NO_PROXY= \
    no_proxy=

RUN printf 'Acquire::http::Proxy "false";\nAcquire::https::Proxy "false";\n' \
      > /etc/apt/apt.conf.d/99disable-proxy \
  && apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libboost-dev \
    libprotobuf-dev \
    libzookeeper-mt-dev \
    pkg-config \
    protobuf-compiler \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /tmp
RUN git -c http.proxy= -c https.proxy= clone https://github.com/chenshuo/muduo.git \
  && cd muduo \
  && git checkout "${MUDUO_REF}" \
  && cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DMUDUO_BUILD_EXAMPLES=OFF \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
  && cmake --build build -j"$(nproc)" \
  && cmake --install build

FROM muduo-builder AS app-builder

WORKDIR /workspace
COPY . /workspace
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -j"$(nproc)"

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ENV http_proxy= \
    https_proxy= \
    HTTP_PROXY= \
    HTTPS_PROXY= \
    ALL_PROXY= \
    all_proxy= \
    NO_PROXY= \
    no_proxy=

RUN printf 'Acquire::http::Proxy "false";\nAcquire::https::Proxy "false";\n' \
      > /etc/apt/apt.conf.d/99disable-proxy \
  && apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libprotobuf32 \
    libzookeeper-mt2 \
    netcat-openbsd \
  && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --uid 10001 --shell /bin/bash app \
  && mkdir -p /workspace /var/log/mprpc /etc/mprpc \
  && chown -R app:app /workspace /var/log/mprpc /etc/mprpc

COPY --from=muduo-builder /usr/local/lib/libmuduo_base.so* /usr/local/lib/
COPY --from=muduo-builder /usr/local/lib/libmuduo_net.so* /usr/local/lib/
COPY --from=app-builder /workspace/bin /workspace/bin
COPY --from=app-builder /workspace/config/mprpc.example.conf /etc/mprpc/mprpc.example.conf
COPY docker/mprpc-entrypoint.sh /usr/local/bin/mprpc-entrypoint.sh
RUN chmod +x /usr/local/bin/mprpc-entrypoint.sh \
  && ldconfig \
  && chown -R app:app /workspace /etc/mprpc /var/log/mprpc

WORKDIR /workspace
ENV HOME=/home/app
ENV PATH="/workspace/bin:${PATH}"
USER app
ENTRYPOINT ["/usr/local/bin/mprpc-entrypoint.sh"]
