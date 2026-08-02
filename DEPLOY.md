# Deploying

Backend runs on Hugging Face Spaces (Docker), frontend on Vercel (static).

**Deploy the backend first.** The frontend now calls `/api/jobs`,
`/api/auth/login` and `/api/jobs/:id/events`. The Space currently serves the old
API (`/api/history`, blocking `/api/mesh`), so shipping the frontend first
leaves the live site calling endpoints that return 404.

---

## 1. Backend — Hugging Face Spaces

### Set the Space secrets first

Settings → *Variables and secrets* on
https://huggingface.co/spaces/swostideep/mesh-stage-API

The container runs with `NODE_ENV=production` and **refuses to start** without
the first two — that is deliberate, so a deployment can never come up with a
throwaway signing key or an in-memory store that quietly loses every account on
restart.

| Name | Required | Notes |
|------|----------|-------|
| `JWT_SECRET` | yes | Any long random string. One generated for you: `3uVdfxa5dcXzlNrB6MQZKAqVefzB2BahTnHKUXUMJ3kqLs0cTW2hMUB3tvunUy6q` |
| `MONGODB_URI` | yes | MongoDB Atlas free tier (M0) is enough. Allow access from `0.0.0.0/0`, since Spaces has no static egress IP. |
| `SM_CORS_ORIGINS` | recommended | Your Vercel URL, e.g. `https://mesh-stage.vercel.app`. Left empty, any origin is accepted. |
| `GOOGLE_CLIENT_ID` | optional | Only needed for Google sign-in; email/password works without it. |
| `REDIS_URL` | optional | Upstash free tier. Without it the queue is in-process, which is fine for one container but loses queued work on restart. |

### Push

```bash
git push hf main
```

The `hf` remote is already configured. Hugging Face asks for your username and
an access token with **write** scope as the password
(https://huggingface.co/settings/tokens).

The Space repo has unrelated history, so the first push needs:

```bash
git push hf main --force
```

Expect roughly 10–15 minutes for the first build: it installs OpenCASCADE,
compiles the engine and runs the test suite as a build step, so a broken engine
fails the build rather than shipping.

### Verify

```bash
curl https://swostideep-mesh-stage-api.hf.space/health
```

Expected:

```json
{"status":"ok","storage":"mongo","queue":"in-process","activeJobs":0}
```

`"storage":"memory"` means `MONGODB_URI` did not reach the container. Check the
Space logs.

---

## 2. Frontend — Vercel

Static files, no build step. Root directory is `sm_frontend`.

`js/api.js` already targets `https://swostideep-mesh-stage-api.hf.space` for any
non-localhost host, so nothing needs editing. Override with `window.SM_API_BASE`
before the module loads if the Space is ever renamed.

Either push to GitHub and let the existing `mesh-stage` project redeploy:

```bash
git push origin main
```

or upload `sm_frontend/` directly in the Vercel dashboard with:

- Framework preset: **Other**
- Root directory: `sm_frontend`
- Build command: *(none)*
- Output directory: `.`

### Verify

Open the deployed URL, register an account, upload a STEP file, and confirm the
engine log streams into the bottom panel and the mesh renders.

---

## Free-tier limits worth knowing

A free Space gives 2 vCPUs and sleeps after ~48 h idle; the first request after
a sleep pays the cold start. The engine is pinned to 2 OpenMP threads and one
job at a time to match, and uploads are capped at 40 MB with a 4-minute engine
timeout.

The Space disk is ephemeral. Generated meshes are swept after 6 hours and are
lost on restart, so job history can outlive the file it points at — the download
route returns 410 in that case rather than a broken file.

## Rolling back

```bash
git log --oneline          # find the commit you want
git push hf <sha>:main --force
```

Or use *Factory reboot* in the Space settings to rebuild from the current
revision.
