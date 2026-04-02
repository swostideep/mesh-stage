// // middlewares/auth.js
// const jwt = require('jsonwebtoken');

// module.exports = function(req, res, next) {
//     // Check for token in headers
//     const token = req.header('Authorization')?.replace('Bearer ', '');

//     if (!token) {
//         return res.status(401).json({ error: 'Access denied. No authentication token provided.' });
//     }

//     try {
//         // Verify token against your .env secret
//         const verified = jwt.verify(token, process.env.JWT_SECRET);
//         req.user = verified; // Attach user info (like userId) to the request object
//         next(); // Pass control to the next route handler
//     } catch (err) {
//         res.status(400).json({ error: 'Invalid or expired token.' });
//     }
// };
// middlewares/auth.js
const jwt = require('jsonwebtoken');

module.exports = function(req, res, next) {
    const token = req.header('Authorization')?.replace('Bearer ', '');

    console.log('\n--- 🔐 SECURITY CHECK ---');
    console.log('Token Received:', token ? `${token.substring(0, 20)}...` : 'NONE');
    console.log('Secret Key Loaded:', process.env.JWT_SECRET ? 'YES' : 'NO');

    if (!token || token === 'null') {
        console.log('❌ Blocked: No token provided.');
        return res.status(401).json({ error: 'Access denied. No authentication token provided.' });
    }

    try {
        const verified = jwt.verify(token, process.env.JWT_SECRET);
        req.user = verified;
        console.log('✅ Passed: User Authorized');
        next();
    } catch (err) {
        console.log('❌ Blocked: Verification Failed ->', err.message);
        res.status(400).json({ error: 'Invalid or expired token.' });
    }
};