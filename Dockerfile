# Multi-stage build for Hugging Face Spaces.
#
# The previous single-stage image kept build-essential and the full
# OpenCASCADE development packages in the shipped layer, which is roughly two
# gigabytes of compilers and headers that never execute. Splitting the build
# out means the runtime image carries only the engine binary, the shared
# libraries it actually links against, and the Node dependencies.

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

# CMake resolves the OpenCASCADE component names itself. Debian renamed the
# toolkits between OCCT 7.5 and 7.8, so a hand-written list of -lTK flags
# breaks the moment the base image moves; letting find_package do it does not.
#
# x86-64-v2 (SSE4.2/POPCNT, Nehalem and later) is a safe floor for cloud
# hardware. -march=native would tune for whichever machine ran the build and
# can fault with an illegal instruction on the machine that runs the container.
RUN cmake -S sm_engine -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DSM_ARCH_FLAGS="-march=x86-64-v2" \
    && cmake --build build -j"$(nproc)" \
    && ./build/sm_tests \
    && strip build/voronoi_mesh

# Collect the engine's runtime shared-library closure so the final stage can
# install just these instead of the whole OCCT development tree.
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

# Spaces runs the container as uid 1000; writing as root leaves files the
# application cannot manage afterwards.
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

# tini reaps the engine process. Without an init, a cancelled mesh leaves a
# zombie behind on every stop, and PID 1 never forwards SIGTERM properly.
ENTRYPOINT ["/usr/bin/tini", "--"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||7860)+'/health').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["node", "server.js"]
