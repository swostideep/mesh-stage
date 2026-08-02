# Multi-stage build for Hugging Face Spaces. The runtime layer carries only the
# engine binary, the shared libraries it links against and the Node
# dependencies - not the ~2 GB of compilers and OCCT headers used to build it.

# ---------------------------------------------------------------- build stage
FROM node:20-bookworm AS engine-build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libocct-data-exchange-dev \
        libocct-foundation-dev \
        libocct-modeling-algorithms-dev \
        libocct-modeling-data-dev \
        libocct-ocaf-dev \
        libocct-visualization-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY sm_engine/ ./sm_engine/

# CMake resolves the OCCT toolkit names, which Debian renamed between 7.5 and
# 7.8. x86-64-v2 is a safe floor for cloud hardware; -march=native would tune
# for the build machine and can fault with an illegal instruction on the host
# that actually runs the container.
RUN cmake -S sm_engine -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DSM_ARCH_FLAGS="-march=x86-64-v2" \
    && cmake --build build -j"$(nproc)" \
    && ./build/sm_tests \
    && strip build/voronoi_mesh

# Collect the engine's runtime shared-library closure for the final stage.
RUN mkdir -p /runtime-libs \
    && ldd build/voronoi_mesh \
       | awk '/=> \//{print $3}' \
       | grep -E 'occt|TK|libomp|libgomp' \
       | xargs -r -I{} cp -L {} /runtime-libs/ \
    && ls -1 /runtime-libs | wc -l

WORKDIR /build/deps
COPY sm_backend/package*.json ./
RUN npm ci --omit=dev --no-audit --no-fund

# -------------------------------------------------------------- runtime stage
FROM node:20-bookworm-slim AS runtime

# Only the OCCT runtime packages, never the -dev variants.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 \
        libstdc++6 \
        tini \
    && rm -rf /var/lib/apt/lists/*

# Spaces runs as uid 1000; writing as root leaves files the app cannot manage.
ENV NODE_ENV=production \
    PORT=7860 \
    SM_UPLOAD_DIR=/data/uploads \
    SM_ENGINE_PATH=/app/engine/voronoi_mesh \
    LD_LIBRARY_PATH=/app/engine/lib \
    OMP_NUM_THREADS=2 \
    SM_THREADS=2

WORKDIR /app

COPY --from=engine-build /build/build/voronoi_mesh /app/engine/voronoi_mesh
COPY --from=engine-build /runtime-libs/ /app/engine/lib/
COPY --from=engine-build /build/deps/node_modules /app/node_modules
COPY sm_backend/ /app/

RUN chmod +x /app/engine/voronoi_mesh \
    && mkdir -p /data/uploads \
    && chown -R node:node /app /data

USER node

EXPOSE 7860

# tini reaps the engine process: without an init, a cancelled mesh leaves a
# zombie on every stop and PID 1 never forwards SIGTERM properly.
ENTRYPOINT ["/usr/bin/tini", "--"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||7860)+'/health').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["node", "server.js"]
