FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libzmq3-dev \
    libflatbuffers-dev \
    flatbuffers-compiler \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Manually download cppzmq headers since Ubuntu 22.04 lacks a dedicated package for them
RUN wget https://raw.githubusercontent.com/zeromq/cppzmq/master/zmq.hpp -O /usr/local/include/zmq.hpp && \
    wget https://raw.githubusercontent.com/zeromq/cppzmq/master/zmq_addon.hpp -O /usr/local/include/zmq_addon.hpp

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
