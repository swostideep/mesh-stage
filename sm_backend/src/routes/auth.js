'use strict';

const express = require('express');
const { OAuth2Client } = require('google-auth-library');

const config = require('../config');
const { issueToken } = require('../middleware/auth');
const { createLimiter } = require('../middleware/rateLimit');
const { httpError, asyncRoute } = require('../middleware/errors');

const EMAIL = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/;

function usernameFrom(email, given) {
    const candidate = (given || '').trim();
    if (candidate) return candidate.slice(0, 48);
    return String(email).split('@')[0].slice(0, 48);
}

function publicUser(user) {
    return { email: user.email, username: user.username, role: user.role };
}

module.exports = function authRoutes(store) {
    const router = express.Router();
    const google = config.auth.googleClientId
        ? new OAuth2Client(config.auth.googleClientId)
        : null;

    const limiter = createLimiter({
        windowMs: config.rateLimit.authWindowMs,
        max: config.rateLimit.authMax
    });

    router.post(
        '/register',
        limiter,
        asyncRoute(async (req, res) => {
            const { email, password, username } = req.body || {};
            if (!EMAIL.test(String(email || ''))) {
                throw httpError(400, 'Enter a valid email address.');
            }
            if (typeof password !== 'string' || password.length < 8) {
                throw httpError(400, 'Password must be at least 8 characters.');
            }
            if (await store.users.findByEmail(email)) {
                throw httpError(409, 'That email is already registered.');
            }

            const user = await store.users.create({
                email,
                password,
                username: usernameFrom(email, username)
            });
            res.status(201).json({ token: issueToken(user), user: publicUser(user) });
        })
    );

    router.post(
        '/login',
        limiter,
        asyncRoute(async (req, res) => {
            const { email, password } = req.body || {};
            const user = await store.users.findByEmail(String(email || ''));
            const ok = await store.users.verifyPassword(user, String(password || ''));
            // One message for both branches so the endpoint cannot be used to
            // enumerate which addresses have accounts.
            if (!user || !ok) throw httpError(401, 'Incorrect email or password.');

            res.json({ token: issueToken(user), user: publicUser(user) });
        })
    );

    router.post(
        '/google',
        limiter,
        asyncRoute(async (req, res) => {
            if (!google) throw httpError(503, 'Google sign-in is not configured.');
            const { token } = req.body || {};
            if (!token) throw httpError(400, 'Missing Google credential.');

            let payload;
            try {
                const ticket = await google.verifyIdToken({
                    idToken: token,
                    audience: config.auth.googleClientId
                });
                payload = ticket.getPayload();
            } catch {
                throw httpError(401, 'Google sign-in could not be verified.');
            }

            const { email, name, sub: googleId } = payload;
            if (!email) throw httpError(401, 'Google account has no email address.');

            let user = await store.users.findByEmail(email);
            if (!user) {
                user = await store.users.create({
                    email,
                    googleId,
                    username: usernameFrom(email, name)
                });
            } else if (!user.googleId) {
                user = await store.users.update(user._id, { googleId });
            }

            res.json({ token: issueToken(user), user: publicUser(user) });
        })
    );

    return router;
};
