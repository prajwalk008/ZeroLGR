FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libzmq3-dev \
    libcppzmq-dev \
    libflatbuffers-dev \
    flatbuffers-compiler \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build the C++ Engine
RUN mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Create final minimal image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libzmq5 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /src/build/zerolgr_engine /app/zerolgr_engine

# The engine uses /var/run/zerolgr for IPC sockets
RUN mkdir -p /var/run/zerolgr

CMD ["/app/zerolgr_engine"]
