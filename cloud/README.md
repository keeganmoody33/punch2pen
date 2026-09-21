# punch2pen cloud — paid profile API

The backend behind **paid / pro** in the plugin: sign-in, a portable per-artist
dictionary, and workspace seats. It is a [Convex](https://docs.convex.dev)
project (TypeScript, HTTP actions) and it is the only server the plugin engine
ever talks to. **Free / lite never contacts it**: with no profile API URL
compiled in, `punch2penEngine` stays on `127.0.0.1:7483` and corrections live
in a session-only dictionary.

No prices, plan names, or billing vendors live here. Entitlement is a
`workspaces.status` switch (`active` / `suspended`) flipped by an operator.

## Layout

```
cloud/
├── convex/
│   ├── schema.ts       users, workspaces, profiles (seats), dictionaryEntries,
│   │                   loginChallenges, sessions
│   ├── http.ts         the HTTP contract the engine + dashboard consume
│   ├── auth.ts         email + six-digit code → bearer session
│   ├── profiles.ts     seat list, dictionary read, correction upsert
│   ├── workspaces.ts   owner-only seat management
│   ├── admin.ts        operator provisioning (npx convex run)
│   ├── mail.ts         login-code delivery (swappable provider)
│   ├── crypto.ts       SHA-256, random tokens/codes (Web Crypto)
│   └── env.ts          environment variables + limits
└── dashboard/
    └── index.html      static seat dashboard for studio / label buyers
```

## Run it

```bash
cd cloud
npm install
npx convex dev            # development deployment; never `convex deploy` from here
```

`npx convex dev` prints the deployment URL. HTTP actions live on the
`.convex.site` host, e.g. `https://happy-otter-123.convex.site`. That is the
value for the engine (`PUNCH2PEN_PROFILE_API_URL`) and the dashboard.

Cloud coding agents: `CONVEX_AGENT_MODE=anonymous npx convex dev` gives an
isolated local deployment on `http://127.0.0.1:3211`.

### Environment variables (`npx convex env set NAME value`)

| Name | Default | Meaning |
|---|---|---|
| `P2P_SIGNUP_MODE` | `invite` | `invite`: only provisioned owners and emails that hold a seat can sign in. `open`: any email signs in and gets a personal workspace of one. |
| `P2P_LOGIN_MAIL_PROVIDER` | unset | `resend` (needs `RESEND_API_KEY`, `P2P_MAIL_FROM`) or `echo` (code returned in the API response; **development only**). Unset refuses sign-in with `delivery_unconfigured`. |
| `RESEND_API_KEY`, `P2P_MAIL_FROM` | — | Only when the provider is `resend`. Swap `mail.ts` to change vendor; nothing else knows. |
| `P2P_DASHBOARD_ORIGIN` | `*` | `Access-Control-Allow-Origin` for the dashboard page. |

### Provision the first buyer (invite mode)

```bash
npx convex run admin:provisionOwner '{"email":"buyer@studio.com","workspaceName":"Studio X"}'
```

Creates the user, a workspace they own, and a seat for themselves. They sign
in on the dashboard with that email, add seats (artist email + display name),
and hand each artist their email. Artists sign in inside the plugin.

Suspend a workspace when a buyer stops paying (whatever that ends up
meaning): `npx convex run admin:setWorkspaceStatus '{"workspaceId":"...","status":"suspended"}'`.
Suspended seats drop the plugin back to free/lite with a message.

## HTTP contract (`/v1`)

All bodies and responses are JSON with an `ok` boolean. Errors:
`{ "ok": false, "error": "<code>", "message": "<human copy>" }`.
Authenticated routes take `Authorization: Bearer p2p_…`.

| Method | Path | Auth | Body → Response |
|---|---|---|---|
| `GET` | `/v1/health` | — | → `{ok, service, apiVersion}` |
| `POST` | `/v1/auth/start` | — | `{email}` → `{ok, delivery:"email"}` or `{ok, delivery:"echo", code}` (dev). Errors `invalid_email`, `not_invited` (403), `rate_limited` (429), `delivery_unconfigured` (503) |
| `POST` | `/v1/auth/verify` | — | `{email, code, device?:{name, platform}}` → `{ok, token, user:{id,email}, profiles:[…]}`. Errors `code_invalid`, `code_expired`, `too_many_attempts`, `not_invited` |
| `POST` | `/v1/auth/logout` | bearer | → `{ok}` |
| `GET` | `/v1/me` | bearer | → `{ok, user, profiles:[{id,name,workspaceId,workspaceName,role,status,dictionaryVersion}]}` |
| `GET` | `/v1/profiles/:id/dictionary` | bearer, seat holder | → `{ok, profileId, version, entries:[{original,corrected,count,updatedAt}]}`. `403 forbidden`, `402 suspended` |
| `POST` | `/v1/profiles/:id/corrections` | bearer, seat holder | `{original, corrected}` → `{ok, version, count}` |
| `GET` | `/v1/workspaces` | bearer | → `{ok, workspaces:[{id,name,status,seatCount}]}` (owned) |
| `POST` | `/v1/workspaces` | bearer, existing owner | `{name}` → `{ok, workspaceId}` |
| `GET` | `/v1/workspaces/:id/seats` | bearer, owner | → `{ok, workspace, seats:[{id,profileName,email,claimed,status,dictionaryVersion}]}` |
| `POST` | `/v1/workspaces/:id/seats` | bearer, owner | `{email, profileName}` → `{ok, seat}`. `409 duplicate_seat` |
| `DELETE` | `/v1/workspaces/:id/seats/:seatId` | bearer, owner | → `{ok}` (seat `revoked`, dictionary rows kept) |

Isolation rule, enforced server-side in `profiles.ts`: only the **seat
holder** (the artist who signed in with that seat's email) can read or write
that seat's dictionary. Owners manage seats; they never see or train another
artist's words.

Login codes are six digits, hashed with a per-challenge salt, expire after ten
minutes, allow five attempts, and are rate-limited to three live codes per
email per ten minutes. Session tokens are 32 random bytes; only their SHA-256
is stored; idle sessions expire after 90 days.

## Engine side

`engine/src/CloudProfileClient.cpp` implements this contract over
`engine/src/IxHttpTransport.cpp`; `engine/src/AccountManager.cpp` owns tier,
session (`~/.punch2pen/account.json`, mode 0600), the active profile, and the
dictionary caches (`~/.punch2pen/profiles/<id>.json`). Point a build at a
deployment with `cmake -DPUNCH2PEN_PROFILE_API_URL=https://….convex.site`,
or without rebuilding via `PUNCH2PEN_PROFILE_API=…` in the environment or
`~/.punch2pen/profile-api.json` `{"url": "…"}`.

## Checks

```bash
npm run typecheck                       # tsc --noEmit over convex/
CONVEX_AGENT_MODE=anonymous npx convex dev --once   # push + validate schema/functions
```

Engine-side contract tests: `cloudProfileClientTest`, `accountManagerTest`,
`dictionaryTest` (see `engine/CMakeLists.txt`).
