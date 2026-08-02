'use strict';

const path = require('path');
const { spawn } = require('child_process');

const config = require('../config');
const logger = require('../logger');
const events = require('./events');

// Child processes are tracked by job id so the stop endpoint can verify
// ownership before signalling, rather than killing whatever is running.
const running = new Map();

const PATTERNS = [
    ['vertices', /Total Nodes\s*:\s*(\d+)/i],
    ['triangles', /Total Elements\s*:\s*(\d+)/i],
    ['maxSkewness', /Max 3D Skewness[^:]*:\s*([0-9.eE+-]+)/i],
    ['meanSkewness', /Mean Skewness\s*:\s*([0-9.eE+-]+)/i],
    ['highSkewCount', /High Skew Elements\s*:\s*(\d+)/i],
    ['freeEdges', /Free Edges[^:]*:\s*(\d+)/i]
];

function parseStats(text, stats) {
    for (const [key, pattern] of PATTERNS) {
        const m = text.match(pattern);
        if (m) stats[key] = Number(m[1]);
    }
}

function isRunning(jobId) {
    return running.has(jobId);
}

function cancel(jobId) {
    const entry = running.get(jobId);
    if (!entry) return false;
    entry.cancelled = true;
    entry.child.kill('SIGKILL');
    return true;
}

function runningCount() {
    return running.size;
}

// Runs the mesher and streams its output to the job's progress channel.
// Resolves with the parsed statistics, rejects on non-zero exit.
function run({ jobId, inputPath, outputPath, density, options = {} }) {
    return new Promise((resolve, reject) => {
        const args = [
            inputPath,
            String(density),
            outputPath,
            String(options.defeatureTol ?? 0.05),
            String(options.patchHoles ?? 'true'),
            String(options.growthRate ?? 1.2),
            String(options.proximity ?? 'true')
        ];

        const child = spawn(config.engine.binary, args, {
            env: {
                ...process.env,
                // Pinned so the engine's pool does not oversubscribe the two
                // cores a free Space actually has.
                OMP_NUM_THREADS: String(config.engine.threads),
                SM_THREADS: String(config.engine.threads),
                SM_MAX_ELEMENTS: String(config.engine.maxElements)
            },
            stdio: ['ignore', 'pipe', 'pipe']
        });

        const entry = { child, cancelled: false };
        running.set(jobId, entry);

        const stats = {};
        let stderrTail = '';

        const timer = setTimeout(() => {
            entry.timedOut = true;
            child.kill('SIGKILL');
        }, config.engine.timeoutMs);
        timer.unref?.();

        child.stdout.setEncoding('utf8');
        child.stdout.on('data', (chunk) => {
            parseStats(chunk, stats);
            events.publish(jobId, { log: chunk });
        });

        child.stderr.setEncoding('utf8');
        child.stderr.on('data', (chunk) => {
            stderrTail = (stderrTail + chunk).slice(-2000);
            events.publish(jobId, { log: `[ERROR] ${chunk}` });
        });

        child.on('error', (err) => {
            clearTimeout(timer);
            running.delete(jobId);
            reject(new Error(`engine could not be started: ${err.message}`));
        });

        child.on('close', (code, signal) => {
            clearTimeout(timer);
            running.delete(jobId);

            if (entry.cancelled) {
                const err = new Error('cancelled');
                err.code = 'CANCELLED';
                return reject(err);
            }
            if (entry.timedOut) {
                const err = new Error(`engine exceeded ${config.engine.timeoutMs} ms`);
                err.code = 'TIMEOUT';
                return reject(err);
            }
            if (code === 0) {
                return resolve(stats);
            }

            logger.warn('engine failed', { jobId, code, signal });
            const err = new Error(
                stderrTail.trim() || `engine exited with code ${code}${signal ? ` (${signal})` : ''}`
            );
            err.code = 'ENGINE_FAILED';
            return reject(err);
        });
    });
}

module.exports = { run, cancel, isRunning, runningCount };
