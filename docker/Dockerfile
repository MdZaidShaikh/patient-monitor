FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

FROM debian:bookworm-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       make \
       sqlite3 \
       libsqlite3-dev \
       libncurses-dev \
       ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . /app

RUN make server device_sim

FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       libsqlite3-0 \
       libncurses6 \
       libtinfo6 \
       ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app/server/patient_server /app/server/patient_server
COPY --from=build /app/device_sim/device_sim /app/device_sim/device_sim

CMD ["bash"]
