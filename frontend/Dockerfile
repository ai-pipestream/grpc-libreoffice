# Build from the repository root so the contract protos are in context:
#   docker build -f frontend/Dockerfile -t grlibre-frontend .
#
# server.mjs resolves ../proto and ../fixtures relative to the app dir, so
# with the app at /app those belong at /proto and /fixtures.
FROM node:24-alpine
WORKDIR /app
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci --omit=dev
COPY frontend/server.mjs ./
COPY frontend/lib ./lib
COPY frontend/public ./public
COPY proto /proto
COPY fixtures /fixtures
ENV PORT=8080
EXPOSE 8080
USER node
# No curl in alpine; busybox wget is. The server answers 200 at UI_BASE.
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s \
  CMD wget -qO /dev/null "http://127.0.0.1:${PORT}${UI_BASE:-/}" || exit 1
CMD ["node", "server.mjs"]
