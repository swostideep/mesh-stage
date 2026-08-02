'use strict';

const crypto = require('crypto');
const fs = require('fs');
const fsp = require('fs/promises');
const path = require('path');

const config = require('../config');
const logger = require('../logger');

fs.mkdirSync(config.uploads.dir, { recursive: true });

// Opaque and unguessable, so one user cannot enumerate another's output.
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

// Every accepted format has a recognisable signature. Checking it stops a
// renamed archive or executable from ever reaching the engine, and it is done
// on content rather than on the extension the uploader chose.
async function sniffCadFile(filePath) {
    const handle = await fsp.open(filePath, 'r');
    try {
        const { size } = await handle.stat();
        const buf = Buffer.alloc(2048);
        const { bytesRead } = await handle.read(buf, 0, 2048, 0);
        const head = buf.subarray(0, bytesRead).toString('latin1');

        if (/ISO-10303-21/i.test(head)) return 'step';
        // OpenCASCADE writes one of these two banners at the top of a .brep.
        if (/CASCADE Topology|DBRep_DrawableShape/i.test(head)) return 'brep';

        // Binary STL is identified by its length, not its header: the spec
        // reserves 80 free-form bytes and plenty of exporters write the word
        // "solid" into them, which would otherwise read as ASCII STL.
        if (size >= 84) {
            const triangles = buf.readUInt32LE(80);
            if (84 + triangles * 50 === size && triangles > 0) return 'stl';
        }
        if (/^\s*solid/i.test(head) && /facet\s+normal/i.test(head)) return 'stl';

        // OBJ has no magic number, so require the two record types a mesh
        // cannot do without: a vertex and a face.
        if (/^\s*v\s+-?[\d.]/m.test(head) && /^\s*f\s+\S/m.test(head)) return 'obj';

        if (/^\s*S\s*0*1/m.test(head) && /[GS]\s*0*\d+\s*$/m.test(head)) return 'iges';
        if (/START RECORD|IGES/i.test(head)) return 'iges';
        return null;
    } finally {
        await handle.close();
    }
}

// Ephemeral container disks are small; without a sweep a long-running Space
// fills up and starts failing uploads for reasons that look unrelated.
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
