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
        allowedExtensions: ['.step', '.stp', '.iges', '.igs']
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

// A missing secret in production is a hard stop; in development we generate an
// ephemeral one so a fresh clone runs without any configuration at all.
if (!config.auth.jwtSecret) {
    if (config.env === 'production') {
        throw new Error('JWT_SECRET must be set in production');
    }
    config.auth.jwtSecret = require('crypto').randomBytes(32).toString('hex');
    config.auth.ephemeralSecret = true;
}

module.exports = config;
