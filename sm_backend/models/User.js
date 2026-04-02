const mongoose = require('mongoose');
const bcrypt = require('bcryptjs');

const userSchema = new mongoose.Schema({
    username: { type: String, required: true },
    email: { type: String, required: true, unique: true },
    password: { type: String }, // Optional for Google users
    googleId: { type: String }, // For Google users
    role: { type: String, enum: ['free', 'pro', 'admin'], default: 'free' },
    createdAt: { type: Date, default: Date.now }
});

// FIXED: Removed the 'next' callback completely. Modern Mongoose just uses return.
userSchema.pre('save', async function() {
    // 1. If password isn't being modified, or doesn't exist (Google login), do nothing and save.
    if (!this.isModified('password') || !this.password) {
        return; 
    }
    
    // 2. Otherwise, hash the newly typed password.
    this.password = await bcrypt.hash(this.password, 10);
});

userSchema.methods.comparePassword = async function(candidatePassword) {
    if (!this.password) return false;
    return await bcrypt.compare(candidatePassword, this.password);
};

module.exports = mongoose.model('User', userSchema);