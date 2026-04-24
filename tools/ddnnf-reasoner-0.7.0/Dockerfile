FROM rust:1.92-bookworm

WORKDIR /app

RUN apt-get update \
      -o Acquire::Retries=5 \
      -o Acquire::http::Timeout="30" \
      -o Acquire::https::Timeout="30" && \
    apt-get install -y --fix-missing --no-install-recommends \
      build-essential \
      pkg-config \
      cmake \
      clang \
      g++ \
      m4 \
      git \
      libgmp-dev \
      libboost-dev \
      libboost-program-options-dev \
    && rm -rf /var/lib/apt/lists/*

COPY . .

RUN cargo build --release

CMD ["/bin/bash"]