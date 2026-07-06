'use strict';

// Fixed-window limiter kept in memory.
//
// Enough to stop credential stuffing and upload floods against a single
// container, which is the deployment this service targets. A multi-instance
// deployment would move the counters into Redis; the interface would not
// change.

function createLimiter({ windowMs, max, keyFn }) {
    const hits = new Map();

    const sweeper = setInterval(() => {
        const now = Date.now();
        for (const [key, entry] of hits) {
            if (entry.resetAt <= now) hits.delete(key);
        }
    }, windowMs);
    sweeper.unref?.();

    return function limit(req, res, next) {
        const key = keyFn ? keyFn(req) : req.ip;
        const now = Date.now();
        let entry = hits.get(key);

        if (!entry || entry.resetAt <= now) {
            entry = { count: 0, resetAt: now + windowMs };
            hits.set(key, entry);
        }

        entry.count += 1;
        const remaining = Math.max(0, max - entry.count);
        res.set('X-RateLimit-Limit', String(max));
        res.set('X-RateLimit-Remaining', String(remaining));

        if (entry.count > max) {
            const retryAfter = Math.ceil((entry.resetAt - now) / 1000);
            res.set('Retry-After', String(retryAfter));
            return res.status(429).json({ error: 'Too many requests. Try again shortly.' });
        }
        return next();
    };
}

module.exports = { createLimiter };
