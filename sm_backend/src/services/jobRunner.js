'use strict';

const logger = require('../logger');
const storage = require('./storage');
const events = require('./events');
const engine = require('./engine');

// The unit of work a queue entry expands into. Kept separate from the HTTP
// routes so the same function serves both queue backends and can be driven
// directly by a test.
function createJobRunner(store) {
    return async function runJob(data) {
        const { jobId, inputKey, storageKey, density, options } = data;
        const inputPath = storage.resolve(inputKey);
        const outputPath = storage.resolve(storageKey);

        await store.jobs.update(jobId, { status: 'processing' });
        events.publish(jobId, { status: 'processing' });

        try {
            const stats = await engine.run({ jobId, inputPath, outputPath, density, options });

            // The engine writes its reference tessellation next to the mesh so
            // the viewer can show the incoming geometry alongside the result.
            const previewKey = storageKey.replace(/\.obj$/, '_geometry.obj');
            const hasPreview = await storage.exists(previewKey);

            await store.jobs.update(jobId, {
                status: 'completed',
                vertices: stats.vertices ?? 0,
                triangles: stats.triangles ?? 0,
                maxSkewness: stats.maxSkewness ?? null,
                meanSkewness: stats.meanSkewness ?? null,
                highSkewCount: stats.highSkewCount ?? 0,
                freeEdges: stats.freeEdges ?? null,
                previewKey: hasPreview ? previewKey : null,
                completedAt: new Date()
            });
            events.publish(jobId, { status: 'completed', stats });
            logger.info('job completed', { jobId, elements: stats.triangles ?? 0 });
        } catch (err) {
            const status = err.code === 'CANCELLED' ? 'cancelled' : 'failed';
            await store.jobs.update(jobId, {
                status,
                // Engine internals are logged, not returned: the message can
                // contain absolute paths from inside the container.
                error: status === 'cancelled' ? null : 'Meshing failed.',
                completedAt: new Date()
            });
            events.publish(jobId, { status, error: err.message });
            logger.warn('job ended', { jobId, status, reason: err.message });
        } finally {
            events.close(jobId);
            // The uploaded CAD file is only needed while the engine runs.
            await storage.remove(inputKey);
        }
    };
}

module.exports = { createJobRunner };
