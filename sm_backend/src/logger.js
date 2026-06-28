'use strict';

// Deliberately tiny. The previous middleware printed bearer tokens and secret
// presence to stdout on every request, which lands verbatim in the hosting
// provider's log viewer. Nothing here ever accepts a credential.

const levels = { error: 0, warn: 1, info: 2, debug: 3 };
const threshold = levels[process.env.SM_LOG_LEVEL] ?? levels.info;

function emit(level, message, fields) {
    if (levels[level] > threshold) return;
    const line = { t: new Date().toISOString(), level, msg: message };
    if (fields) Object.assign(line, fields);
    const out = level === 'error' ? process.stderr : process.stdout;
    out.write(`${JSON.stringify(line)}\n`);
}

module.exports = {
    error: (m, f) => emit('error', m, f),
    warn: (m, f) => emit('warn', m, f),
    info: (m, f) => emit('info', m, f),
    debug: (m, f) => emit('debug', m, f)
};
