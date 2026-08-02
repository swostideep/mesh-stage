'use strict';

require('dotenv').config();

const path = require('path');

function int(value, fallback) {
    const n = Number.parseInt(value, 10);
    return Number.isFinite(n) ? n : fallback;
}

function list(value, fallback) {
    if (!value) return fallback;
    return value.split(',').map((s) => s.trim()).filter(Boolean);
}

const root = path.resolve(__dirname, '..');

const config = {
    env: process.env.NODE_ENV || 'development',
    port: int(process.env.PORT, 7860),

    // Hugging Face Spaces gives a free Space two vCPUs. Handing the engine
    // more OpenMP threads than that only adds context switching, and running
    // more than one job at a time makes both jobs slower without finishing
    // either sooner.
    engine: {
        binary: process.env.SM_ENGINE_PATH || path.join(root, 'engine', 'voronoi_mesh'),
        threads: int(process.env.SM_THREADS, 2),
        maxElements: int(process.env.SM_MAX_ELEMENTS, 1500000),
        timeoutMs: int(process.env.SM_ENGINE_TIMEOUT_MS, 240000),
        concurrency: int(process.env.SM_CONCURRENCY, 1)
    },

    uploads: {
        dir: process.env.SM_UPLOAD_DIR || path.join(root, 'uploads'),
        maxBytes: int(process.env.SM_MAX_UPLOAD_BYTES, 40 * 1024 * 1024),
        // Free-tier disks are small and ephemeral. Anything older than this
        // is swept so a long-running Space cannot fill its own volume.
        retentionMs: int(process.env.SM_RETENTION_MS, 6 * 60 * 60 * 1000),
        sweepIntervalMs: int(process.env.SM_SWEEP_INTERVAL_MS, 15 * 60 * 1000),
        // STEP/IGES/BREP carry real geometry and get meshed. STL/OBJ arrive
        // already triangulated and are passed through for inspection only.
        allowedExtensions: ['.step', '.stp', '.iges', '.igs', '.brep', '.brp', '.stl', '.obj']
    },

    auth: {
        jwtSecret: process.env.JWT_SECRET,
        tokenTtl: process.env.SM_TOKEN_TTL || '24h',
        googleClientId: process.env.GOOGLE_CLIENT_ID
    },

    // Empty list means "reflect any origin", which is only sensible in
    // development. Production deployments should pin this.
    corsOrigins: list(process.env.SM_CORS_ORIGINS, []),

    rateLimit: {
        authWindowMs: int(process.env.SM_AUTH_WINDOW_MS, 15 * 60 * 1000),
        authMax: int(process.env.SM_AUTH_MAX, 20),
        jobWindowMs: int(process.env.SM_JOB_WINDOW_MS, 60 * 60 * 1000),
        jobMax: int(process.env.SM_JOB_MAX, 40)
    },

    mongoUri: process.env.MONGODB_URI || '',
    redisUrl: process.env.REDIS_URL || ''
};

// Production is a hard stop on missing configuration; development fills in an
// ephemeral secret so a fresh clone runs with no setup at all.
//
// Everything missing is reported at once. Failing on the first one costs a
// whole rebuild per variable to discover the next, which on a platform that
// takes ten minutes to build an image is a bad way to spend an afternoon.
if (config.env === 'production') {
    const missing = [];
    if (!config.auth.jwtSecret) {
        missing.push('JWT_SECRET      any long random string; signs session tokens');
    }
    if (!config.mongoUri) {
        missing.push('MONGODB_URI     connection string; without it accounts vanish on restart');
    }
    if (missing.length > 0) {
        config.missingProductionVars = missing;
    }
} else if (!config.auth.jwtSecret) {
    config.auth.jwtSecret = require('crypto').randomBytes(32).toString('hex');
    config.auth.ephemeralSecret = true;
}

module.exports = config;
