# Build stage: JDK plus LibreOffice, so the integration tests convert real
# documents before an image can exist.
FROM eclipse-temurin:25-jdk AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
      libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-draw \
      fonts-liberation fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN ./gradlew --no-daemon build :grlibre-service:installDist

# Runtime: JRE plus LibreOffice, nothing else. All writable paths live under
# /tmp, so the container runs with a read-only root filesystem and a tmpfs
# mounted at /tmp.
FROM eclipse-temurin:25-jre
RUN apt-get update && apt-get install -y --no-install-recommends \
      libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-draw \
      fonts-liberation fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --home-dir /tmp/grlibre --shell /usr/sbin/nologin grlibre
COPY --from=build /src/grlibre-service/build/install/grlibre-service /opt/grlibre
USER grlibre
ENV HOME=/tmp/grlibre
EXPOSE 50053
ENTRYPOINT ["/opt/grlibre/bin/grlibre-service"]
