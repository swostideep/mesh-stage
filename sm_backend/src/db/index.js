'use strict';

const config = require('../config');
const logger = require('../logger');

async function createStore() {
    if (config.mongoUri) {
        const store = require('./mongo').createStore(config.mongoUri);
        await store.connect();
        logger.info('storage ready', { backend: 'mongodb' });
        return store;
    }

    if (config.env === 'production') {
        throw new Error('MONGODB_URI is required when NODE_ENV=production');
    }

    const store = require('./memory').createStore();
    await store.connect();
    logger.warn('storage ready', {
        backend: 'memory',
        note: 'no MONGODB_URI set; data is lost on restart'
    });
    return store;
}

module.exports = { createStore };
