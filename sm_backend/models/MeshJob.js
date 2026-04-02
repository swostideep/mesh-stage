// models/MeshJob.js
const mongoose = require('mongoose');

const meshJobSchema = new mongoose.Schema({
    userId: { type: mongoose.Schema.Types.ObjectId, ref: 'User', required: true },
    originalFilename: { type: String, required: true },
    densityTarget: { type: Number, required: true },
    
    // Track the BullMQ status
    status: { type: String, enum: ['pending', 'processing', 'completed', 'failed'], default: 'pending' },
    
    // FEA Quality Diagnostics
    vertices: { type: Number, default: 0 },
    triangles: { type: Number, default: 0 },
    maxSkewness: { type: Number, default: null },
    highSkewCount: { type: Number, default: 0 },
    
    // File Storage
    meshUrl: { type: String, default: null },
    
    createdAt: { type: Date, default: Date.now },
    completedAt: { type: Date, default: null }
});

module.exports = mongoose.model('MeshJob', meshJobSchema);