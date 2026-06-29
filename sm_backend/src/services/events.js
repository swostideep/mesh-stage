'use strict';

// Per-job progress channels.
//
// The previous implementation kept one global array of response objects and
// wrote every engine line to all of them. Any signed-in user watching the
// progress stream therefore received the filenames and engine output of every
// other user's job. Streams are now keyed by job id and the route that opens
// one verifies ownership first.

const MAX_BUFFERED_LINES = 400;

class ProgressHub {
    constructor() {
        this.channels = new Map();
    }

    channel(jobId) {
        let ch = this.channels.get(jobId);
        if (!ch) {
            ch = { clients: new Set(), buffer: [], closed: false };
            this.channels.set(jobId, ch);
        }
        return ch;
    }

    publish(jobId, payload) {
        const ch = this.channel(jobId);
        // Replayed to late subscribers so a client that connects a moment
        // after the job starts does not miss the opening phase.
        ch.buffer.push(payload);
        if (ch.buffer.length > MAX_BUFFERED_LINES) ch.buffer.shift();

        const frame = `data: ${JSON.stringify(payload)}\n\n`;
        for (const res of ch.clients) {
            try {
                res.write(frame);
            } catch {
                ch.clients.delete(res);
            }
        }
    }

    subscribe(jobId, res) {
        const ch = this.channel(jobId);
        for (const item of ch.buffer) res.write(`data: ${JSON.stringify(item)}\n\n`);
        if (ch.closed) {
            res.write('event: end\ndata: {}\n\n');
            res.end();
            return () => {};
        }
        ch.clients.add(res);
        return () => ch.clients.delete(res);
    }

    close(jobId) {
        const ch = this.channels.get(jobId);
        if (!ch) return;
        ch.closed = true;
        for (const res of ch.clients) {
            try {
                res.write('event: end\ndata: {}\n\n');
                res.end();
            } catch {
                /* client already gone */
            }
        }
        ch.clients.clear();
        // Keep the buffer briefly so a client reconnecting after completion
        // still sees the tail of the log rather than an empty stream.
        setTimeout(() => this.channels.delete(jobId), 60000).unref?.();
    }
}

module.exports = new ProgressHub();
