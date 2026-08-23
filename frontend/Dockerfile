# Build from the repository root so the contract protos are in context:
#   docker build -f frontend/Dockerfile -t grpc-libreoffice-ui .
#
# server.mjs resolves ../proto and ../fixtures relative to the app dir, so
# with the app at /app those belong at /proto and /fixtures.
#
# Both stages are Docker Hardened Images. The runtime has no shell, no
# package manager and no wget: every build step happens in the -dev stage
# and the healthcheck runs through node itself.
FROM dhi.io/node:24-alpine-dev AS build
WORKDIR /app
COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci --omit=dev
COPY frontend/server.mjs ./
COPY frontend/lib ./lib
COPY frontend/public ./public

FROM dhi.io/node:24-alpine
WORKDIR /app
COPY --from=build /app /app
COPY proto /proto
COPY fixtures /fixtures
ENV PORT=8080
EXPOSE 8080
# The hardened runtime already runs as uid 1000. No wget/curl exists, so the
# healthcheck asks node; the server answers 200 at UI_BASE.
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s \
  CMD ["node", "-e", "fetch(`http://127.0.0.1:${process.env.PORT||8080}${process.env.UI_BASE||''}/`).then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"]
CMD ["node", "server.mjs"]
