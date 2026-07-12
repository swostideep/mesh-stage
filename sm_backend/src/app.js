'use strict';

const cors = require('cors');
const express = require('express');

const config = require('./config');
const logger = require('./logger');
const engine = require('./services/engine');
const storage = require('./services/storage');
const authRoutes = require('./routes/auth');
const meshRoutes = require('./routes/mesh');
const { notFound, errorHandler } = require('./middleware/errors');

// Written by hand rather than pulling in a helmet dependency: the API serves
// JSON and file downloads only, so it needs four headers, and every megabyte
// left out of the image is a faster cold start on a free container.
function securityHeaders(req, res, next) {
    res.set('X-Content-Type-Options', 'nosniff');
    res.set('X-Frame-Options', 'DENY');
    res.set('Referrer-Policy', 'no-referrer');
    res.set('Cross-Origin-Resource-Policy', 'cross-origin');
    res.removeHeader('X-Powered-By');
    next();
}

function buildCors() {
    if (config.corsOrigins.length === 0) {
        logger.warn('cors allows any origin', { hint: 'set SM_CORS_ORIGINS in production' });
        return cors({ origin: true, credentials: false });
    }
    return cors({
        origin(origin, cb) {
            if (!origin || config.corsOrigins.includes(origin)) return cb(null, true);
            return cb(new Error('Origin not allowed'));
        },
        methods: ['GET', 'POST', 'DELETE', 'OPTIONS'],
        allowedHeaders: ['Content-Type', 'Authorization']
    });
}

function createApp(store, queue) {
    const app = express();

    app.disable('x-powered-by');
    app.set('trust proxy', 1);
    app.use(securityHeaders);
    app.use(buildCors());
    app.use(express.json({ limit: '64kb' }));
    app.use(express.urlencoded({ extended: false, limit: '64kb' }));

    // Liveness and readiness in one place; the hosting platform polls this to
    // decide whether the container came up.
    app.get('/health', async (req, res) => {
        let depth = null;
        try {
            depth = await queue.depth();
        } catch {
            depth = null;
        }
        res.json({
            status: 'ok',
            uptimeSeconds: Math.round(process.uptime()),
            storage: store.kind,
            queue: queue.kind,
            queueDepth: depth,
            activeJobs: engine.runningCount(),
            memoryMb: Math.round(process.memoryUsage().rss / 1048576)
        });
    });

    app.use('/api/auth', authRoutes(store));
    app.use('/api', meshRoutes(store, queue));

    app.use(notFound);
    app.use(errorHandler);

    storage.startSweeper();
    return app;
}

module.exports = { createApp };
