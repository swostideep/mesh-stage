require('dotenv').config();
const express = require('express');
const mongoose = require('mongoose');
const multer = require('multer');
const path = require('path');
const fs = require('fs');
const jwt = require('jsonwebtoken');
const { spawn } = require('child_process');
const { Queue, Worker, QueueEvents } = require('bullmq');
const Redis = require('ioredis');

// ADDED: Google Auth Library
const { OAuth2Client } = require('google-auth-library');
const client = new OAuth2Client(process.env.GOOGLE_CLIENT_ID);

// Import Models & Middleware
const User = require('./models/User');
const MeshJob = require('./models/MeshJob');
const auth = require('./middlewares/auth'); 

const cors = require('cors'); // ADDED: Import CORS

const app = express();

// ADDED: CORS Security (Allows Vercel to connect to this backend)
app.use(cors({
    origin: '*', // We can restrict this to your exact Vercel URL later
    methods: ['GET', 'POST', 'DELETE', 'OPTIONS'],
    allowedHeaders: ['Content-Type', 'Authorization']
}));

app.use(express.json()); 

// KEPT: We only serve the uploads folder so users can download their .obj files
app.use('/uploads', auth, express.static(path.join(__dirname, 'uploads')));

// DELETED public and assets (Vercel handles those now)

if (!fs.existsSync('uploads')) fs.mkdirSync('uploads');

// --- DATABASE CONNECTION ---
mongoose.connect(process.env.MONGODB_URI)
    .then(() => console.log('[SYSTEM] Connected to MongoDB.'))
    .catch(err => console.error('[ERROR] MongoDB connection failed:', err));

// --- QUEUE SETUP ---
const redisConnection = new Redis(process.env.REDIS_URL, { 
    maxRetriesPerRequest: null,
    tls: {
        rejectUnauthorized: false
    }
});
const meshQueue = new Queue('meshQueue', { connection: redisConnection });
const queueEvents = new QueueEvents('meshQueue', { connection: redisConnection });

const storage = multer.diskStorage({
    destination: (req, file, cb) => cb(null, 'uploads/'),
    filename: (req, file, cb) => cb(null, Date.now() + '-' + file.originalname.replace(/\s+/g, '_'))
});
const upload = multer({ storage });
let sseClients = [];

// --- THE WORKER DAEMON ---
const worker = new Worker('meshQueue', async (job) => {
    const { inputPath, density, outputFilename, jobId, defeatureTol, patchHoles, growthRate, proximity } = job.data;
    const outputPath = path.join(__dirname, 'uploads', outputFilename);
    await MeshJob.findByIdAndUpdate(jobId, { status: 'processing' });

    return new Promise((resolve, reject) => {
        broadcastSSE({ log: `\n[QUEUE] Processing ${path.basename(inputPath)}...` });
        const enginePath = path.join(__dirname, 'engine', 'voronoi_mesh'); 
        const engine = spawn(enginePath, [inputPath, density, outputPath, defeatureTol, patchHoles, growthRate, proximity]);
        let finalStats = { v: 0, t: 0, skew: 0, bad: 0 };
     
    engine.stdout.on('data', (data) => {
            const output = data.toString();
            console.log(output); // Shows in Render logs
            broadcastSSE({ log: output }); // Sends to frontend

            // 1. Smart match for Vertices or Nodes (case-insensitive)
            const vMatch = output.match(/(?:Vertices|Nodes|Total Nodes)\s*[:=]?\s*(\d+)/i);
            if (vMatch) finalStats.v = parseInt(vMatch[1]);

            // 2. Smart match for Triangles or Elements
            const tMatch = output.match(/(?:Triangles|Elements|Total Elements)\s*[:=]?\s*(\d+)/i);
            if (tMatch) finalStats.t = parseInt(tMatch[1]);

            // 3. Strict match for Max Skewness (prevents grabbing element IDs like 26675)
            const skewMatch = output.match(/Max(?:imum)? 3D Skewness[^:]*[:=]\s*([0-9.]+)/i);
            if (skewMatch) finalStats.skew = parseFloat(skewMatch[1]);

            // 4. Strict match for Bad/High Skew Elements
            const badMatch = output.match(/(?:High Skew|Bad) Elements[^:]*[:=]\s*(\d+)/i);
            if (badMatch) finalStats.bad = parseInt(badMatch[1]);
        });

        engine.stderr.on('data', (data) => broadcastSSE({ log: `[ERROR] ${data.toString()}` }));

        engine.on('close', async (code) => {
            if (code === 0) {
                await MeshJob.findByIdAndUpdate(jobId, {
                    status: 'completed', vertices: finalStats.v, triangles: finalStats.t, maxSkewness: finalStats.skew,
                    highSkewCount: finalStats.bad, meshUrl: `/uploads/${outputFilename}`, completedAt: new Date()
                });
                broadcastSSE({ log: `[QUEUE] Job completed successfully.` });
                resolve({ meshUrl: `/uploads/${outputFilename}` });
            } else {
                await MeshJob.findByIdAndUpdate(jobId, { status: 'failed' });
                reject(new Error(`Engine exited with code ${code}`));
            }
        });
    });
}, { connection: redisConnection, concurrency: 1 });

// ==========================================
// --- REST API ROUTES ---
// ==========================================

// --- GOOGLE AUTHENTICATION ---
// --- GOOGLE AUTHENTICATION (DEBUG VERSION) ---
app.post('/api/auth/google', async (req, res) => {
    try {
        const { token } = req.body;
        
        // 1. Verify Google Token
        const ticket = await client.verifyIdToken({ 
            idToken: token, 
            audience: process.env.GOOGLE_CLIENT_ID 
        });
        
        const payload = ticket.getPayload();
        const { email, name, sub: googleId } = payload; 

        // 2. Find or Create User
        let user = await User.findOne({ email });
        
        if (!user) {
            user = new User({ email, googleId, username: name || email.split('@')[0] });
            await user.save();
        } else {
            let dbChanged = false;
            if (!user.googleId) { user.googleId = googleId; dbChanged = true; }
            if (!user.username) { user.username = name || email.split('@')[0]; dbChanged = true; }
            if (dbChanged) await user.save();
        }

        // 3. Issue JWT Token
        const appToken = jwt.sign({ userId: user._id, role: user.role }, process.env.JWT_SECRET, { expiresIn: '24h' });
        res.json({ token: appToken, user: { email: user.email, username: user.username } });

    } catch (err) {
        console.error('\n[ERROR] Google Auth failed:', err.message);
        // THIS IS THE MAGIC LINE: It sends the actual crash reason to your screen
        res.status(500).json({ error: `Server Crash Reason: ${err.message}` });
    }
});

app.post('/api/register', async (req, res) => {
    try {
        const { email, password, username } = req.body;
        const existingUser = await User.findOne({ email });
        if (existingUser) return res.status(400).json({ error: 'Email already in use.' });

        const user = new User({ email, password, username: username || email.split('@')[0] });
        await user.save();
        
        const token = jwt.sign({ userId: user._id, role: user.role }, process.env.JWT_SECRET, { expiresIn: '24h' });
        res.json({ token, user: { email: user.email, username: user.username } });
    } catch (err) {
        console.error('\n[CRITICAL ERROR] Registration failed:', err);
        res.status(500).json({ error: `Server crashed: ${err.message}` });
    }
});

app.post('/api/login', async (req, res) => {
    try {
        const { email, password } = req.body;
        const user = await User.findOne({ email });
        if (!user) return res.status(400).json({ error: 'Invalid credentials.' });

        const isMatch = await user.comparePassword(password);
        if (!isMatch) return res.status(400).json({ error: 'Invalid credentials.' });

        // Fallback for undefined legacy usernames
        const finalUsername = user.username || user.email.split('@')[0];

        const token = jwt.sign({ userId: user._id, role: user.role }, process.env.JWT_SECRET, { expiresIn: '24h' });
        res.json({ token, user: { email: user.email, username: finalUsername } });
    } catch (err) {
        console.error('\n[CRITICAL ERROR] Login failed:', err);
        res.status(500).json({ error: `Server crashed: ${err.message}` });
    }
});

app.post('/api/mesh', [auth, upload.single('cadFile')], async (req, res) => {
    if (!req.file) return res.status(400).json({ error: 'No file uploaded' });
    const density = req.body.density || 0.05;
    const outputFilename = `mesh_${Date.now()}.obj`;

   try {
        const newJob = new MeshJob({ userId: req.user.userId, originalFilename: req.file.originalname, densityTarget: density });
        await newJob.save();

        const job = await meshQueue.add('process-cad', {
            inputPath: req.file.path, density: density, outputFilename: outputFilename, jobId: newJob._id,
            defeatureTol: req.body.defeatureTol || 0.05, patchHoles: req.body.patchHoles || 'true',
            growthRate: req.body.growthRate || 1.2, proximity: req.body.proximity || 'true'
        });

        broadcastSSE({ log: `[SERVER] Uploaded ${req.file.originalname}. Added to Queue.` });
        const result = await job.waitUntilFinished(queueEvents);
        res.json({ success: true, meshUrl: result.meshUrl });
    } catch (error) { res.status(500).json({ success: false, error: 'Failed to process mesh.' }); }
});
// Add this near your other routes
app.post('/api/mesh/stop', auth, (req, res) => {
    try {
        // This kills any process named 'voronoi_mesh' owned by the server
        const kill = spawn('pkill', ['-u', 'node', '-9', 'voronoi_mesh']);
        
        kill.on('close', (code) => {
            broadcastSSE({ log: "\n[SYSTEM] Meshing process forcibly terminated by user." });
            res.json({ success: true, message: "Engine stopped." });
        });
    } catch (err) {
        res.status(500).json({ error: "Failed to kill process." });
    }
});
app.get('/api/history', auth, async (req, res) => {
    try {
        const jobs = await MeshJob.find({ userId: req.user.userId, status: 'completed' }).sort({ createdAt: -1 }).limit(10);
        res.json(jobs);
    } catch (err) { res.status(500).json({ error: 'Could not fetch history.' }); }
});

app.get('/api/download/:jobId', auth, async (req, res) => {
    try {
        const job = await MeshJob.findOne({ _id: req.params.jobId, userId: req.user.userId });
        if (!job || !job.meshUrl) return res.status(404).json({ error: 'Mesh not found.' });
        const filePath = path.join(__dirname, 'uploads', path.basename(job.meshUrl));
        if (!fs.existsSync(filePath)) return res.status(404).json({ error: 'File deleted.' });
        res.download(filePath, `SM_${job.originalFilename}.obj`);
    } catch (err) { res.status(500).json({ error: 'Download failed.' }); }
});

app.delete('/api/mesh/:jobId', auth, async (req, res) => {
    try {
        const job = await MeshJob.findOne({ _id: req.params.jobId, userId: req.user.userId });
        if (!job) return res.status(404).json({ error: 'Job not found.' });
        if (job.meshUrl) {
            const filePath = path.join(__dirname, 'uploads', path.basename(job.meshUrl));
            if (fs.existsSync(filePath)) fs.unlinkSync(filePath);
        }
        await MeshJob.findByIdAndDelete(req.params.jobId);
        res.json({ success: true, message: 'Job deleted.' });
    } catch (err) { res.status(500).json({ error: 'Failed to delete.' }); }
});

app.get('/api/progress', (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.setHeader('Connection', 'keep-alive');
    res.flushHeaders();
    sseClients.push(res);
    req.on('close', () => { sseClients = sseClients.filter(c => c !== res); });
});


function broadcastSSE(data) { sseClients.forEach(client => client.write(`data: ${JSON.stringify(data)}\n\n`)); }

const PORT = process.env.PORT || 7860;
app.listen(PORT, () => { console.log(`[SYSTEM] Surface Mesher API running on http://localhost:${PORT}`); });