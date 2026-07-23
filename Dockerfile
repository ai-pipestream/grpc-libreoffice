# Build stage: toolchain, LibreOfficeKit headers, and a full LibreOffice so
# the render tests run real conversions before an image can exist.
FROM ubuntu:26.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build git ca-certificates libgoogle-perftools-dev \
      libreofficekit-dev libreoffice-dev \
      libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-draw \
      libreoffice-math \
      fonts-liberation fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGRLIBRE_WERROR=ON \
    && cmake --build build \
    && ctest --test-dir build --output-on-failure \
         -R 'event-frame-test|png-encode-test|worker-render-test|render-service-test|docling-map-test'

# Runtime: LibreOffice, fonts, and the two binaries. All writable paths live
# under /tmp, so the container runs read-only with a tmpfs at /tmp.
FROM ubuntu:26.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-draw \
      libreoffice-math \
      libtcmalloc-minimal4t64 \
      fonts-liberation fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home-dir /tmp/grlibre --shell /usr/sbin/nologin grlibre
COPY --from=build /src/build/grlibre-server /src/build/grlibre-worker /opt/grlibre/
USER grlibre
ENV HOME=/tmp/grlibre
EXPOSE 50053
ENTRYPOINT ["/opt/grlibre/grlibre-server"]
