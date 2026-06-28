'use strict';

const crypto = require('crypto');
const bcrypt = require('bcryptjs');

// In-process store used when no MONGODB_URI is configured.
//
// It exists so the API runs from a fresh clone with no external services,
// which makes local verification of the whole pipeline a single command. It is
// not a production store: everything is lost on restart, and the process
// refuses to start in this mode when NODE_ENV is production.

function id() {
    return crypto.randomBytes(12).toString('hex');
}

function createStore() {
    const users = new Map();
    const jobs = new Map();

    return {
        kind: 'memory',
        async connect() {},
        async disconnect() {},

        users: {
            async findByEmail(email) {
                for (const u of users.values()) if (u.email === email) return { ...u };
                return null;
            },
            async findById(userId) {
                const u = users.get(userId);
                return u ? { ...u } : null;
            },
            async create({ email, password, username, googleId, role }) {
                const record = {
                    _id: id(),
                    email,
                    username,
                    googleId: googleId || null,
                    role: role || 'free',
                    passwordHash: password ? await bcrypt.hash(password, 10) : null,
                    createdAt: new Date()
                };
                users.set(record._id, record);
                return { ...record };
            },
            async update(userId, patch) {
                const u = users.get(userId);
                if (!u) return null;
                Object.assign(u, patch);
                return { ...u };
            },
            async verifyPassword(user, candidate) {
                if (!user || !user.passwordHash) return false;
                return bcrypt.compare(candidate, user.passwordHash);
            }
        },

        jobs: {
            async create(doc) {
                const record = { _id: id(), createdAt: new Date(), ...doc };
                jobs.set(record._id, record);
                return { ...record };
            },
            async findById(jobId) {
                const j = jobs.get(jobId);
                return j ? { ...j } : null;
            },
            async update(jobId, patch) {
                const j = jobs.get(jobId);
                if (!j) return null;
                Object.assign(j, patch);
                return { ...j };
            },
            async listByUser(userId, limit) {
                return [...jobs.values()]
                    .filter((j) => j.userId === userId)
                    .sort((a, b) => b.createdAt - a.createdAt)
                    .slice(0, limit)
                    .map((j) => ({ ...j }));
            },
            async remove(jobId) {
                return jobs.delete(jobId);
            }
        }
    };
}

module.exports = { createStore };
