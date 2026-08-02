'use strict';

const config = require('../config');
const logger = require('../logger');

// Two interchangeable queues behind one interface. BullMQ when REDIS_URL is
// set, so work survives a restart; an in-process FIFO otherwise, so the service
// runs from a fresh clone with no external dependency. Both honour the same
// concurrency limit.

function createInProcessQueue(handler) {
    const pending = [];
    let active = 0;

    function pump() {
        while (active < config.engine.concurrency && pending.length > 0) {
            const task = pending.shift();
            active += 1;
            Promise.resolve()
                .then(() => handler(task.data))
                .catch((err) => logger.error('job failed', { jobId: task.data.jobId, error: err.message }))
                .finally(() => {
                    active -= 1;
                    pump();
                });
        }
    }

    return {
        kind: 'in-process',
        async add(data) {
            pending.push({ data });
            setImmediate(pump);
        },
        async depth() {
            return pending.length + active;
        },
        async close() {
            pending.length = 0;
        }
    };
}

function createRedisQueue(handler) {
    const { Queue, Worker } = require('bullmq');
    const IORedis = require('ioredis');

    const useTls = /^rediss:/i.test(config.redisUrl);
    const connection = new IORedis(config.redisUrl, {
        maxRetriesPerRequest: null,
        enableReadyCheck: false,
        // Certificate validation stays on; disabling it would defeat the
        // point of the rediss:// scheme.
        ...(useTls ? { tls: {} } : {})
    });

    connection.on('error', (err) => logger.error('redis error', { error: err.message }));

    const queue = new Queue('meshQueue', { connection });
    const worker = new Worker('meshQueue', async (job) => handler(job.data), {
        connection,
        concurrency: config.engine.concurrency
    });

    worker.on('failed', (job, err) => {
        logger.error('job failed', { jobId: job?.data?.jobId, error: err.message });
    });

    return {
        kind: 'redis',
        async add(data) {
            await queue.add('mesh', data, {
                removeOnComplete: 50,
                removeOnFail: 50,
                attempts: 1
            });
        },
        async depth() {
            return queue.getWaitingCount();
        },
        async close() {
            await worker.close();
            await queue.close();
            await connection.quit();
        }
    };
}

function createQueue(handler) {
    if (config.redisUrl) {
        try {
            const q = createRedisQueue(handler);
            logger.info('queue ready', { backend: 'redis' });
            return q;
        } catch (err) {
            logger.error('redis queue unavailable, falling back', { error: err.message });
        }
    }
    const q = createInProcessQueue(handler);
    logger.info('queue ready', { backend: 'in-process', concurrency: config.engine.concurrency });
    return q;
}

module.exports = { createQueue };
