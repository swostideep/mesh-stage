// Single place the API host is configured. Override at runtime with
// window.SM_API_BASE, or serve from localhost to pick up the dev port.

const fromGlobal = typeof window !== 'undefined' && window.SM_API_BASE;
const isLocal =
    typeof location !== 'undefined' &&
    /^(localhost|127\.0\.0\.1|\[::1\])$/.test(location.hostname);

export const API_BASE =
    fromGlobal || (isLocal ? 'http://localhost:7860' : 'https://swostideep-mesh-stage-api.hf.space');

export function getToken() {
    return localStorage.getItem('token');
}

export function clearSession() {
    localStorage.removeItem('token');
    localStorage.removeItem('sm_username');
}

class ApiError extends Error {
    constructor(message, status) {
        super(message);
        this.status = status;
    }
}

export async function apiFetch(path, options = {}) {
    const token = getToken();
    const headers = new Headers(options.headers || {});
    if (token) headers.set('Authorization', `Bearer ${token}`);
    if (options.json !== undefined) {
        headers.set('Content-Type', 'application/json');
        options.body = JSON.stringify(options.json);
        options.method = options.method || 'POST';
    }

    const response = await fetch(`${API_BASE}${path}`, { ...options, headers });

    // An expired session should return the user to the login screen rather
    // than surfacing as an unexplained failure on every subsequent action.
    if (response.status === 401) {
        clearSession();
        if (!location.pathname.endsWith('auth.html')) location.href = 'auth.html';
        throw new ApiError('Session expired.', 401);
    }

    if (!response.ok) {
        let message = `Request failed (${response.status})`;
        try {
            const body = await response.json();
            if (body && body.error) message = body.error;
        } catch {
            /* non-JSON error body */
        }
        throw new ApiError(message, response.status);
    }
    return response;
}

export async function apiJson(path, options) {
    const res = await apiFetch(path, options);
    return res.json();
}

export function eventStream(jobId) {
    const token = encodeURIComponent(getToken() || '');
    return new EventSource(`${API_BASE}/api/jobs/${jobId}/events?token=${token}`);
}
