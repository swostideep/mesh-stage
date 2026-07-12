'use strict';

const config = require('./src/config');
const logger = require('./src/logger');
const { createStore } = require('./src/db');
const { createQueue } = require('./src/services/queue');
const { createJobRunner } = require('./src/services/jobRunner');
const { createApp } = require('./src/app');

async function main() {
    const store = await createStore();
    const queue = createQueue(createJobRunner(store));
    const app = createApp(store, queue);

    const server = app.listen(config.port, () => {
        logger.info('api listening', {
            port: config.port,
            env: config.env,
            storage: store.kind,
            queue: queue.kind,
            engineThreads: config.engine.threads
        });
        if (config.auth.ephemeralSecret) {
            logger.warn('using a generated JWT secret', {
                note: 'sessions will not survive a restart; set JWT_SECRET'
            });
        }
    });

    // Without this the container is killed mid-job on every deploy: in-flight
    // work is abandoned and its database row is left saying "processing"
    // forever, which the dashboard then shows as a stuck entry.
    let shuttingDown = false;
    async function shutdown(signal) {
        if (shuttingDown) return;
        shuttingDown = true;
        logger.info('shutting down', { signal });

        server.close(() => logger.info('http server closed'));
        const force = setTimeout(() => {
            logger.warn('forcing exit');
            process.exit(1);
        }, 15000);
        force.unref?.();

        try {
            await queue.close();
            await store.disconnect();
        } catch (err) {
            logger.error('shutdown error', { error: err.message });
        }
        clearTimeout(force);
        process.exit(0);
    }

    process.on('SIGTERM', () => shutdown('SIGTERM'));
    process.on('SIGINT', () => shutdown('SIGINT'));
    process.on('unhandledRejection', (reason) => {
        logger.error('unhandled rejection', { error: String(reason) });
    });
}

main().catch((err) => {
    logger.error('failed to start', { error: err.message });
    process.exit(1);
});
