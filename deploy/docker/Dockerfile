FROM ubuntu:24.04@sha256:b3cc40b72b93588182b5410f723c7aaf142363311c2aa993d8a453ddcbb3ae15 AS deps
ENV DEBIAN_FRONTEND=noninteractive VCPKG_ROOT=/opt/vcpkg VCPKG_MAX_CONCURRENCY=3
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build git curl zip unzip tar pkg-config ca-certificates autoconf automake libtool python3 linux-libc-dev && rm -rf /var/lib/apt/lists/*
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg && git -C /opt/vcpkg checkout fa8cecf91d7f31a1715a7a6524f208897ffb33ce && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
WORKDIR /opt/dependencies
COPY vcpkg.json ./
RUN /opt/vcpkg/vcpkg install --clean-after-build

FROM deps AS build
WORKDIR /src
COPY . .
RUN cmake -S . -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_INSTALLED_DIR=/opt/dependencies/vcpkg_installed -DCMAKE_INSTALL_PREFIX=/opt/runyard \
    && cmake --build /tmp/build -j 3 && ctest --test-dir /tmp/build --output-on-failure \
    && cmake --install /tmp/build

FROM ubuntu:24.04@sha256:b3cc40b72b93588182b5410f723c7aaf142363311c2aa993d8a453ddcbb3ae15 AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libstdc++6 \
    && rm -rf /var/lib/apt/lists/* && useradd --uid 10001 --create-home runyard \
    && mkdir /work && chown runyard:runyard /work
WORKDIR /work
USER 10001:10001

FROM runtime AS runner-base
COPY --from=build /opt/runyard/bin/runyard-runner /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/runyard-runner"]

FROM runner-base AS fixture
COPY --from=build /opt/runyard/bin/runyard-fixture /usr/local/bin/

FROM runtime AS server
COPY --from=build /opt/runyard/bin/runyard-server /usr/local/bin/
COPY migrations /opt/runyard/migrations
ENTRYPOINT ["/usr/local/bin/runyard-server"]
CMD ["serve"]

FROM runtime AS cli
COPY --from=build /opt/runyard/bin/runyard /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/runyard"]

FROM runtime AS agent
USER 0:0
COPY --from=build /opt/runyard/bin/runyard-agent /usr/local/bin/
ENTRYPOINT ["/usr/local/bin/runyard-agent"]
