'use strict';

const express = require('express');
const fs = require('fs');
const multer = require('multer');
const path = require('path');

const config = require('../config');
const storage = require('../services/storage');
const events = require('../services/events');
const engine = require('../services/engine');
const { authenticate } = require('../middleware/auth');
const { createLimiter } = require('../middleware/rateLimit');
const { httpError, asyncRoute } = require('../middleware/errors');

function clampDensity(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return 0.05;
    return Math.min(0.3, Math.max(0.01, n));
}

function publicJob(job) {
    return {
        id: job._id,
        originalFilename: job.originalFilename,
        densityTarget: job.densityTarget,
        status: job.status,
        vertices: job.vertices,
        triangles: job.triangles,
        maxSkewness: job.maxSkewness,
        meanSkewness: job.meanSkewness,
        highSkewCount: job.highSkewCount,
        minAngle: job.minAngle,
        maxAspectRatio: job.maxAspectRatio,
        minScaledJacobian: job.minScaledJacobian,
        highAspectCount: job.highAspectCount,
        freeEdges: job.freeEdges,
        inconsistentEdges: job.inconsistentEdges,
        error: job.error,
        createdAt: job.createdAt,
        completedAt: job.completedAt,
        // Relative, opaque, and only resolvable by the owner.
        meshUrl: job.status === 'completed' ? `/api/jobs/${job._id}/mesh` : null,
        geometryUrl: job.previewKey ? `/api/jobs/${job._id}/geometry` : null
    };
}

module.exports = function meshRoutes(store, queue) {
    const router = express.Router();

    const upload = multer({
        storage: multer.diskStorage({
            destination: (req, file, cb) => cb(null, config.uploads.dir),
            filename: (req, file, cb) => {
                const ext = path.extname(file.originalname).toLowerCase();
                cb(null, storage.newKey(ext));
            }
        }),
        limits: { fileSize: config.uploads.maxBytes, files: 1 },
        fileFilter: (req, file, cb) => {
            const ext = path.extname(file.originalname).toLowerCase();
            if (!config.uploads.allowedExtensions.includes(ext)) {
                return cb(httpError(415, 'Accepted formats: STEP, IGES, BREP, STL, OBJ.'));
            }
            return cb(null, true);
        }
    });

    const jobLimiter = createLimiter({
        windowMs: config.rateLimit.jobWindowMs,
        max: config.rateLimit.jobMax,
        keyFn: (req) => req.user?.userId || req.ip
    });

    // Loads the job and proves the caller owns it. Every per-job route goes
    // through here, so an id from another account is indistinguishable from
    // one that does not exist.
    const loadOwnedJob = asyncRoute(async (req, res, next) => {
        const job = await store.jobs.findById(req.params.jobId);
        if (!job || String(job.userId) !== String(req.user.userId)) {
            throw httpError(404, 'Job not found.');
        }
        req.job = job;
        next();
    });

    router.post(
        '/mesh',
        authenticate,
        jobLimiter,
        upload.single('cadFile'),
        asyncRoute(async (req, res) => {
            if (!req.file) throw httpError(400, 'No CAD file was uploaded.');

            const kind = await storage.sniffCadFile(req.file.path);
            if (!kind) {
                await storage.remove(req.file.filename);
                throw httpError(415, 'That file is not a readable CAD or mesh model.');
            }

            const density = clampDensity(req.body.density);
            const job = await store.jobs.create({
                userId: req.user.userId,
                originalFilename: String(req.file.originalname).slice(0, 200),
                densityTarget: density,
                status: 'queued',
                storageKey: storage.newKey('.obj'),
                inputKey: req.file.filename
            });

            await queue.add({
                jobId: String(job._id),
                inputKey: job.inputKey,
                storageKey: job.storageKey,
                density,
                options: {
                    defeatureTol: req.body.defeatureTol,
                    patchHoles: req.body.patchHoles,
                    growthRate: req.body.growthRate,
                    proximity: req.body.proximity
                }
            });

            // Returns immediately: holding the request open until the engine
            // finished would kill any mesh slower than the proxy timeout.
            res.status(202).json({ job: publicJob(job) });
        })
    );

    router.get(
        '/jobs',
        authenticate,
        asyncRoute(async (req, res) => {
            const limit = Math.min(50, Math.max(1, Number(req.query.limit) || 20));
            const jobs = await store.jobs.listByUser(req.user.userId, limit);
            res.json({ jobs: jobs.map(publicJob) });
        })
    );

    router.get('/jobs/:jobId', authenticate, loadOwnedJob, (req, res) => {
        res.json({ job: publicJob(req.job) });
    });

    router.get('/jobs/:jobId/events', authenticate, loadOwnedJob, (req, res) => {
        res.set({
            'Content-Type': 'text/event-stream',
            'Cache-Control': 'no-cache, no-transform',
            Connection: 'keep-alive',
            'X-Accel-Buffering': 'no'
        });
        res.flushHeaders?.();

        const unsubscribe = events.subscribe(req.params.jobId, res);

        // Proxies in front of a free-tier host drop idle connections; a
        // periodic comment frame keeps the stream alive without polluting it.
        const ping = setInterval(() => res.write(': ping\n\n'), 20000);
        ping.unref?.();

        req.on('close', () => {
            clearInterval(ping);
            unsubscribe();
        });
    });

    router.post('/jobs/:jobId/cancel', authenticate, loadOwnedJob, (req, res) => {
        const stopped = engine.cancel(String(req.job._id));
        res.json({ cancelled: stopped });
    });

    function sendAsset(keyField, downloadName) {
        return (req, res, next) => {
            const key = req.job[keyField];
            if (!key) return next(httpError(404, 'File not available.'));
            const full = storage.resolve(key);
            if (!full || !fs.existsSync(full)) {
                return next(httpError(410, 'This file has expired and was removed.'));
            }
            return res.download(full, downloadName(req.job));
        };
    }

    router.get(
        '/jobs/:jobId/mesh',
        authenticate,
        loadOwnedJob,
        sendAsset('storageKey', (job) => `SM_${path.parse(job.originalFilename).name}.obj`)
    );

    router.get(
        '/jobs/:jobId/geometry',
        authenticate,
        loadOwnedJob,
        sendAsset('previewKey', (job) => `SM_${path.parse(job.originalFilename).name}_geometry.obj`)
    );

    router.delete(
        '/jobs/:jobId',
        authenticate,
        loadOwnedJob,
        asyncRoute(async (req, res) => {
            engine.cancel(String(req.job._id));
            await storage.remove(req.job.storageKey);
            await storage.remove(req.job.previewKey);
            await storage.remove(req.job.inputKey);
            await store.jobs.remove(String(req.job._id));
            res.json({ deleted: true });
        })
    );

    return router;
};
