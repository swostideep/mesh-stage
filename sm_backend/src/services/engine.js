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
    ['minAngle', /Min Angle[^:]*:\s*([0-9.eE+-]+)/i],
    ['maxAspectRatio', /Max Aspect Ratio\s*:\s*([0-9.eE+-]+|inf)/i],
    ['minScaledJacobian', /Min Scaled Jacobian\s*:\s*([0-9.eE+-]+)/i],
    ['highAspectCount', /High Aspect Elements\s*:\s*(\d+)/i],
    ['freeEdges', /Free Edges[^:]*:\s*(\d+)/i],
    ['inconsistentEdges', /Inconsistent Edges\s*:\s*(\d+)/i]
];

function parseStats(text, stats) {
    for (const [key, pattern] of PATTERNS) {
        const m = text.match(pattern);
        if (!m) continue;
        const value = Number(m[1]);
        // `inf` would parse to Infinity and poison the document; the engine
        // clamps aspect ratio for this reason, so treat it as unreported.
        if (Number.isFinite(value)) stats[key] = value;
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
            String(options.growthRate ?? 1.2)
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
        let stdoutTail = '';

        const timer = setTimeout(() => {
            entry.timedOut = true;
            child.kill('SIGKILL');
        }, config.engine.timeoutMs);
        timer.unref?.();

        child.stdout.setEncoding('utf8');
        child.stdout.on('data', (chunk) => {
            // The live log goes out unbuffered; only the scrape waits for a
            // newline. A report line split across a pipe boundary would
            // otherwise match nothing and the metric would silently be absent.
            events.publish(jobId, { log: chunk });

            stdoutTail += chunk;
            const lastBreak = stdoutTail.lastIndexOf('\n');
            if (lastBreak >= 0) {
                parseStats(stdoutTail.slice(0, lastBreak + 1), stats);
                stdoutTail = stdoutTail.slice(lastBreak + 1);
            }
            // Guard against an engine that emits megabytes without a newline.
            if (stdoutTail.length > 65536) stdoutTail = stdoutTail.slice(-8192);
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
            // A last line with no trailing newline is still worth parsing.
            if (stdoutTail) parseStats(stdoutTail, stats);

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

// parseStats is exported for the chunk-splitting regression test.
module.exports = { run, cancel, isRunning, runningCount, parseStats };
