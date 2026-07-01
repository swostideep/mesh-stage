'use strict';

const crypto = require('crypto');
const fs = require('fs');
const fsp = require('fs/promises');
const path = require('path');

const config = require('../config');
const logger = require('../logger');

fs.mkdirSync(config.uploads.dir, { recursive: true });

// Opaque, unguessable names. Job outputs were previously called
// `mesh_<Date.now()>.obj`, which any authenticated user could enumerate.
function newKey(suffix) {
    return `${crypto.randomBytes(16).toString('hex')}${suffix}`;
}

function resolve(key) {
    if (typeof key !== 'string' || !key) return null;
    // Reject anything that is not a bare filename so a crafted key cannot
    // escape the upload directory.
    const base = path.basename(key);
    if (base !== key) return null;
    return path.join(config.uploads.dir, base);
}

async function exists(key) {
    const p = resolve(key);
    if (!p) return false;
    try {
        await fsp.access(p, fs.constants.R_OK);
        return true;
    } catch {
        return false;
    }
}

async function remove(key) {
    const p = resolve(key);
    if (!p) return;
    try {
        await fsp.unlink(p);
    } catch {
        /* already gone */
    }
}

// STEP and IGES are text formats with a recognisable preamble. Checking it
// stops a renamed archive or executable from ever reaching the engine.
async function sniffCadFile(filePath) {
    const handle = await fsp.open(filePath, 'r');
    try {
        const buf = Buffer.alloc(2048);
        const { bytesRead } = await handle.read(buf, 0, 2048, 0);
        const head = buf.subarray(0, bytesRead).toString('latin1');
        if (/ISO-10303-21/i.test(head)) return 'step';
        if (/^\s*S\s*0*1/m.test(head) && /[GS]\s*0*\d+\s*$/m.test(head)) return 'iges';
        if (/START RECORD|IGES/i.test(head)) return 'iges';
        return null;
    } finally {
        await handle.close();
    }
}

// Sweeps files older than the retention window. Ephemeral container disks are
// small, and a Space that runs for a week otherwise fills up and starts
// failing uploads for reasons that look unrelated.
async function sweep() {
    const cutoff = Date.now() - config.uploads.retentionMs;
    let removed = 0;
    try {
        const entries = await fsp.readdir(config.uploads.dir);
        for (const name of entries) {
            const full = path.join(config.uploads.dir, name);
            try {
                const st = await fsp.stat(full);
                if (st.isFile() && st.mtimeMs < cutoff) {
                    await fsp.unlink(full);
                    removed += 1;
                }
            } catch {
                /* raced with another sweep */
            }
        }
    } catch (err) {
        logger.warn('sweep failed', { error: err.message });
        return 0;
    }
    if (removed > 0) logger.info('swept expired files', { removed });
    return removed;
}

function startSweeper() {
    const timer = setInterval(() => {
        sweep().catch(() => {});
    }, config.uploads.sweepIntervalMs);
    timer.unref?.();
    return timer;
}

module.exports = { newKey, resolve, exists, remove, sniffCadFile, sweep, startSweeper };
