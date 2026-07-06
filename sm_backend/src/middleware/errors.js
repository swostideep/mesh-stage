'use strict';

const logger = require('../logger');
const config = require('../config');

function notFound(req, res) {
    res.status(404).json({ error: 'Not found.' });
}

// Routes throw; this is the only place that decides what the client is told.
// Internal messages used to be returned verbatim, which leaked stack detail,
// driver errors and configuration state to anyone who could trigger a fault.
function errorHandler(err, req, res, _next) {
    const status = err.status || 500;
    if (status >= 500) {
        logger.error('request failed', {
            method: req.method,
            path: req.path,
            error: err.message,
            stack: config.env === 'production' ? undefined : err.stack
        });
    }
    const body = { error: status >= 500 ? 'Internal server error.' : err.message };
    if (err.code) body.code = err.code;
    res.status(status).json(body);
}

function httpError(status, message, code) {
    const err = new Error(message);
    err.status = status;
    if (code) err.code = code;
    return err;
}

// Wraps an async handler so a rejected promise reaches errorHandler instead of
// silently hanging the request.
function asyncRoute(handler) {
    return (req, res, next) => Promise.resolve(handler(req, res, next)).catch(next);
}

module.exports = { notFound, errorHandler, httpError, asyncRoute };
