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
# The license texts of everything statically linked into the two binaries:
# gRPC and the dependencies it bundles (abseil, protobuf with upb, RE2,
# c-ares, BoringSSL, zlib, utf8_range, address_sorting, xxHash), libwebp,
# and stb, next to this project's own license.
RUN set -e; deps=build/_deps; tp=$deps/grpc-src/third_party; out=build/licenses; \
    mkdir -p $out; \
    cp LICENSE $out/grpc-libreoffice-LICENSE; \
    cp $deps/grpc-src/LICENSE $out/grpc-LICENSE; \
    cp $deps/grpc-src/NOTICE.txt $out/grpc-NOTICE.txt; \
    cp $tp/abseil-cpp/LICENSE $out/abseil-cpp-LICENSE; \
    cp $tp/protobuf/LICENSE $out/protobuf-LICENSE; \
    cp $tp/re2/LICENSE $out/re2-LICENSE; \
    cp $tp/cares/cares/LICENSE.md $out/c-ares-LICENSE.md; \
    cp $tp/boringssl-with-bazel/LICENSE $out/boringssl-LICENSE; \
    cp $tp/zlib/LICENSE $out/zlib-LICENSE; \
    cp $tp/utf8_range/LICENSE $out/utf8_range-LICENSE; \
    cp $tp/address_sorting/LICENSE $out/address_sorting-LICENSE; \
    cp $tp/xxhash/LICENSE $out/xxhash-LICENSE; \
    cp $deps/libwebp-src/COPYING $out/libwebp-COPYING; \
    cp $deps/libwebp-src/PATENTS $out/libwebp-PATENTS; \
    sed -n '/^This software is available under 2 licenses/,$p' \
      third_party/stb/stb_image_write.h > $out/stb-LICENSE; \
    test -s $out/stb-LICENSE

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
COPY --from=build /src/build/licenses /opt/grlibre/licenses/
USER grlibre
ENV HOME=/tmp/grlibre
ENV GRLIBRE_TMPFS_DIR=/tmp
EXPOSE 50053
ENTRYPOINT ["/opt/grlibre/grlibre-server"]
