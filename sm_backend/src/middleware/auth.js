'use strict';

const jwt = require('jsonwebtoken');
const config = require('../config');

function readToken(req) {
    const header = req.get('authorization');
    if (header && header.startsWith('Bearer ')) return header.slice(7).trim();
    // EventSource cannot set headers, so the progress stream passes its token
    // as a query parameter. Nothing else accepts one there.
    if (req.query && typeof req.query.token === 'string') return req.query.token.trim();
    return null;
}

function authenticate(req, res, next) {
    const token = readToken(req);
    if (!token || token === 'null' || token === 'undefined') {
        return res.status(401).json({ error: 'Authentication required.' });
    }
    try {
        req.user = jwt.verify(token, config.auth.jwtSecret);
        return next();
    } catch {
        // The reason is deliberately not echoed back; distinguishing an
        // expired token from a forged one is useful to an attacker.
        return res.status(401).json({ error: 'Invalid or expired session.' });
    }
}

function issueToken(user) {
    return jwt.sign({ userId: String(user._id), role: user.role }, config.auth.jwtSecret, {
        expiresIn: config.auth.tokenTtl
    });
}

module.exports = { authenticate, issueToken };
