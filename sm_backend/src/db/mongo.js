'use strict';

const mongoose = require('mongoose');
const bcrypt = require('bcryptjs');

const userSchema = new mongoose.Schema({
    username: { type: String, required: true },
    email: { type: String, required: true, unique: true, index: true },
    passwordHash: { type: String, default: null },
    googleId: { type: String, default: null },
    role: { type: String, enum: ['free', 'pro', 'admin'], default: 'free' },
    createdAt: { type: Date, default: Date.now }
});

const jobSchema = new mongoose.Schema({
    userId: { type: mongoose.Schema.Types.ObjectId, ref: 'User', required: true, index: true },
    originalFilename: { type: String, required: true },
    densityTarget: { type: Number, required: true },
    status: {
        type: String,
        enum: ['queued', 'processing', 'completed', 'failed', 'cancelled'],
        default: 'queued'
    },
    // Random, not derived from a timestamp. Sequential names let any signed-in
    // user walk the upload directory by guessing neighbouring values.
    storageKey: { type: String, required: true, index: true },
    previewKey: { type: String, default: null },
    inputKey: { type: String, default: null },
    vertices: { type: Number, default: 0 },
    triangles: { type: Number, default: 0 },
    maxSkewness: { type: Number, default: null },
    meanSkewness: { type: Number, default: null },
    highSkewCount: { type: Number, default: 0 },
    freeEdges: { type: Number, default: null },
    error: { type: String, default: null },
    createdAt: { type: Date, default: Date.now },
    completedAt: { type: Date, default: null }
});

jobSchema.index({ userId: 1, createdAt: -1 });

function plain(doc) {
    if (!doc) return null;
    const o = doc.toObject ? doc.toObject() : doc;
    o._id = String(o._id);
    if (o.userId) o.userId = String(o.userId);
    return o;
}

function createStore(uri) {
    const User = mongoose.models.User || mongoose.model('User', userSchema);
    const MeshJob = mongoose.models.MeshJob || mongoose.model('MeshJob', jobSchema);

    return {
        kind: 'mongo',
        async connect() {
            await mongoose.connect(uri, { serverSelectionTimeoutMS: 8000 });
        },
        async disconnect() {
            await mongoose.connection.close();
        },

        users: {
            async findByEmail(email) {
                return plain(await User.findOne({ email }));
            },
            async findById(userId) {
                return plain(await User.findById(userId));
            },
            async create({ email, password, username, googleId, role }) {
                const passwordHash = password ? await bcrypt.hash(password, 10) : null;
                return plain(
                    await User.create({ email, username, googleId, role, passwordHash })
                );
            },
            async update(userId, patch) {
                return plain(await User.findByIdAndUpdate(userId, patch, { new: true }));
            },
            async verifyPassword(user, candidate) {
                if (!user || !user.passwordHash) return false;
                return bcrypt.compare(candidate, user.passwordHash);
            }
        },

        jobs: {
            async create(doc) {
                return plain(await MeshJob.create(doc));
            },
            async findById(jobId) {
                if (!mongoose.isValidObjectId(jobId)) return null;
                return plain(await MeshJob.findById(jobId));
            },
            async update(jobId, patch) {
                if (!mongoose.isValidObjectId(jobId)) return null;
                return plain(await MeshJob.findByIdAndUpdate(jobId, patch, { new: true }));
            },
            async listByUser(userId, limit) {
                const rows = await MeshJob.find({ userId }).sort({ createdAt: -1 }).limit(limit);
                return rows.map(plain);
            },
            async remove(jobId) {
                if (!mongoose.isValidObjectId(jobId)) return false;
                const res = await MeshJob.findByIdAndDelete(jobId);
                return Boolean(res);
            }
        }
    };
}

module.exports = { createStore };
