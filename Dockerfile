# Build stage: toolchain, LibreOfficeKit headers, and a full LibreOffice so
# the render tests run real conversions before an image can exist.
FROM ubuntu:26.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      g++ cmake ninja-build git ca-certificates libgoogle-perftools-dev \
      poppler-utils \
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
         -R 'event-frame-test|png-encode-test|worker-runner-test|worker-render-test|render-service-test|docling-map-test'

# Runtime: LibreOffice, fonts, and the two binaries. All writable paths live
# under /tmp, so the container runs read-only with a tmpfs at /tmp; the
# server verifies at startup that it really is tmpfs (uploaded documents
# stay in RAM, never on disk) and refuses to run otherwise.
#
# No GPL code ships: LibreOffice's PDF import (xpdfimport, libpdfimportlo,
# and its registry fragment) is the only consumer of GPL Poppler here, and
# the service refuses PDF input, so dpkg is told not to unpack the import
# and Poppler is purged after the install. libreoffice-core keeps its
# declared dependency on Poppler unsatisfied, which matters only to apt
# runs after this layer; this image makes none. The RUN fails if any of it
# is still on disk, and scripts/smoke-test.sh checks the published image.
FROM ubuntu:26.04
RUN printf '%s\n' \
      'path-exclude=/usr/lib/libreoffice/program/xpdfimport' \
      'path-exclude=/usr/lib/libreoffice/program/libpdfimportlo.so' \
      'path-exclude=/usr/lib/libreoffice/share/registry/pdfimport.xcd' \
      > /etc/dpkg/dpkg.cfg.d/grlibre-no-pdf-import \
    && apt-get update && apt-get install -y --no-install-recommends \
      libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-draw \
      libreoffice-math \
      libtcmalloc-minimal4t64 \
      fonts-liberation fonts-dejavu-core \
    && poppler="$(dpkg-query -W -f '${db:Status-Abbrev}${Package}\n' 'libpoppler*' \
         | awk '$1 == "ii" { print $2 }')" \
    && if [ -n "$poppler" ]; then dpkg --purge --force-depends $poppler; fi \
    && rm -rf /var/lib/apt/lists/* \
    && test -z "$(find / -xdev \( -name 'libpoppler*' -o -name 'xpdfimport' \
         -o -name 'libpdfimportlo.so' -o -name 'pdfimport.xcd' \) -print -quit)" \
    && useradd --system --home-dir /tmp/grlibre --shell /usr/sbin/nologin grlibre
COPY --from=build /src/build/grlibre-server /src/build/grlibre-worker /opt/grlibre/
USER grlibre
ENV HOME=/tmp/grlibre
ENV GRLIBRE_TMPFS_DIR=/tmp
EXPOSE 50053
ENTRYPOINT ["/opt/grlibre/grlibre-server"]
