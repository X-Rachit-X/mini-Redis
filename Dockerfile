# Two stages: build with the full compiler toolchain, then copy only the
# binaries into a small runtime image.

FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make -j"$(nproc)"

FROM ubuntu:24.04
COPY --from=build /src/bin/mini-redis-server /src/bin/mini-redis-cli /usr/local/bin/
# The AOF lives in /data, so mount a volume there to keep data across restarts.
WORKDIR /data
VOLUME /data
EXPOSE 6379
# Inside a container we must listen on 0.0.0.0, or nothing outside the
# container can reach us. Who can actually connect is decided by `docker run -p`.
ENTRYPOINT ["mini-redis-server", "--bind", "0.0.0.0", "--aof-file", "/data/appendonly.aof"]
