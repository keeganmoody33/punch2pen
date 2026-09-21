import { httpRouter } from "convex/server";
import { internal } from "./_generated/api";
import type { Id } from "./_generated/dataModel";
import { httpAction, type ActionCtx } from "./_generated/server";
import type { SessionInfo } from "./auth";
import {
  hashLoginCode,
  normalizeEmail,
  randomHex,
  randomLoginCode,
  randomSessionToken,
  sha256Hex,
} from "./crypto";
import { dashboardOrigin, signupMode } from "./env";
import { deliverLoginCode } from "./mail";

// HTTP contract consumed by engine/src/CloudProfileClient.cpp and
// cloud/dashboard/index.html. Every response is JSON with an `ok` flag.
// Bearer session tokens; the plugin never holds a vendor key.

export const API_VERSION = 1;

type JsonRecord = Record<string, unknown>;

function corsHeaders(): Record<string, string> {
  return {
    "Access-Control-Allow-Origin": dashboardOrigin(),
    "Access-Control-Allow-Methods": "GET, POST, DELETE, OPTIONS",
    "Access-Control-Allow-Headers": "Authorization, Content-Type",
    "Access-Control-Max-Age": "86400",
    Vary: "Origin",
  };
}

function json(status: number, body: JsonRecord): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json", ...corsHeaders() },
  });
}

function fail(status: number, error: string, message: string): Response {
  return json(status, { ok: false, error, message });
}

async function readJson(request: Request): Promise<JsonRecord | null> {
  try {
    const parsed: unknown = await request.json();
    if (typeof parsed === "object" && parsed !== null && !Array.isArray(parsed)) {
      return parsed as JsonRecord;
    }
    return null;
  } catch {
    return null;
  }
}

function stringField(body: JsonRecord, key: string): string | null {
  const value = body[key];
  return typeof value === "string" ? value : null;
}

function bearerToken(request: Request): string | null {
  const header = request.headers.get("Authorization") ?? "";
  const match = /^Bearer\s+(\S+)$/i.exec(header);
  return match?.[1] ?? null;
}

const SESSION_TOUCH_INTERVAL_MS = 60 * 60 * 1000;

async function requireSession(
  ctx: ActionCtx,
  request: Request,
): Promise<SessionInfo | Response> {
  const token = bearerToken(request);
  if (token === null) {
    return fail(401, "unauthorized", "Missing bearer token");
  }
  const now = Date.now();
  const session = await ctx.runQuery(internal.auth.sessionForToken, {
    tokenHash: await sha256Hex(token),
    now,
  });
  if (session === null) {
    return fail(401, "unauthorized", "Session is invalid or expired");
  }
  if (now - session.lastSeenAt > SESSION_TOUCH_INTERVAL_MS) {
    await ctx.runMutation(internal.auth.touchSession, {
      sessionId: session.sessionId,
      now,
    });
  }
  return session;
}

function pathSegments(request: Request): string[] {
  return new URL(request.url).pathname.split("/").filter((s) => s.length > 0);
}

function deviceFields(body: JsonRecord): { deviceName: string; platform: string } {
  const device = body["device"];
  let deviceName = "punch2pen plugin";
  let platform = "unknown";
  if (typeof device === "object" && device !== null && !Array.isArray(device)) {
    const rec = device as JsonRecord;
    deviceName = stringField(rec, "name")?.slice(0, 80) ?? deviceName;
    platform = stringField(rec, "platform")?.slice(0, 40) ?? platform;
  }
  return { deviceName, platform };
}

// ── auth ────────────────────────────────────────────────────────────────

const authStart = httpAction(async (ctx, request) => {
  const body = await readJson(request);
  if (body === null) {
    return fail(400, "bad_request", "Body must be JSON");
  }
  const email = normalizeEmail(stringField(body, "email") ?? "");
  if (email === null) {
    return fail(400, "invalid_email", "Enter a valid email address");
  }
  const mode = signupMode();
  const allowed = await ctx.runQuery(internal.auth.emailAllowed, {
    email,
    signupMode: mode,
  });
  if (!allowed) {
    return fail(
      403,
      "not_invited",
      "No seat for this email yet. Ask your studio or workspace owner.",
    );
  }

  const code = randomLoginCode();
  const salt = randomHex(16);
  const created = await ctx.runMutation(internal.auth.createChallenge, {
    email,
    codeHash: await hashLoginCode(code, salt),
    salt,
    now: Date.now(),
  });
  if (!created.ok) {
    return fail(429, "rate_limited", "Too many codes requested. Wait ten minutes.");
  }

  const delivery = await deliverLoginCode(email, code);
  if (!delivery.ok) {
    return fail(
      503,
      delivery.error,
      delivery.error === "delivery_unconfigured"
        ? "Sign-in code delivery is not configured on this deployment."
        : "Could not send the sign-in code. Try again.",
    );
  }
  if (delivery.provider === "echo") {
    return json(200, { ok: true, delivery: "echo", code: delivery.code });
  }
  return json(200, { ok: true, delivery: "email" });
});

const authVerify = httpAction(async (ctx, request) => {
  const body = await readJson(request);
  if (body === null) {
    return fail(400, "bad_request", "Body must be JSON");
  }
  const email = normalizeEmail(stringField(body, "email") ?? "");
  const code = (stringField(body, "code") ?? "").replace(/\s+/g, "");
  if (email === null || !/^\d{6}$/.test(code)) {
    return fail(400, "code_invalid", "Enter the six-digit code from your email");
  }
  const now = Date.now();
  const open = await ctx.runQuery(internal.auth.latestOpenChallenge, { email, now });
  if (open === null) {
    return fail(400, "code_expired", "That code has expired. Request a new one.");
  }

  const token = randomSessionToken();
  const { deviceName, platform } = deviceFields(body);
  const result = await ctx.runMutation(internal.auth.verifyChallenge, {
    challengeId: open.challengeId,
    email,
    codeHash: await hashLoginCode(code, open.salt),
    tokenHash: await sha256Hex(token),
    deviceName,
    platform,
    signupMode: signupMode(),
    now,
  });
  if (!result.ok) {
    const error = result.error;
    switch (error) {
      case "code_invalid":
        return fail(400, error, "That code did not match");
      case "code_expired":
        return fail(400, error, "That code has expired. Request a new one.");
      case "too_many_attempts":
        return fail(429, error, "Too many attempts. Request a new code.");
      case "not_invited":
        return fail(403, error, "No seat for this email yet.");
      default: {
        const exhaustive: never = error;
        return exhaustive;
      }
    }
  }

  const profiles = await ctx.runQuery(internal.profiles.listForUser, {
    userId: result.userId,
  });
  return json(200, {
    ok: true,
    apiVersion: API_VERSION,
    token,
    user: { id: result.userId, email: result.email },
    profiles,
  });
});

const authLogout = httpAction(async (ctx, request) => {
  const session = await requireSession(ctx, request);
  if (session instanceof Response) {
    return session;
  }
  await ctx.runMutation(internal.auth.revokeSession, {
    sessionId: session.sessionId,
    now: Date.now(),
  });
  return json(200, { ok: true });
});

const me = httpAction(async (ctx, request) => {
  const session = await requireSession(ctx, request);
  if (session instanceof Response) {
    return session;
  }
  const profiles = await ctx.runQuery(internal.profiles.listForUser, {
    userId: session.userId,
  });
  return json(200, {
    ok: true,
    apiVersion: API_VERSION,
    user: { id: session.userId, email: session.email },
    profiles,
  });
});

// ── profiles / dictionary ───────────────────────────────────────────────

const profileRoutes = httpAction(async (ctx, request) => {
  // /v1/profiles/:id/dictionary (GET) | /v1/profiles/:id/corrections (POST)
  const segments = pathSegments(request);
  const profileId = segments[2] as Id<"profiles"> | undefined;
  const leaf = segments[3];
  if (profileId === undefined || leaf === undefined || segments.length !== 4) {
    return fail(404, "not_found", "Unknown profile route");
  }
  const session = await requireSession(ctx, request);
  if (session instanceof Response) {
    return session;
  }

  if (leaf === "dictionary" && request.method === "GET") {
    const result = await ctx.runQuery(internal.profiles.getDictionary, {
      userId: session.userId,
      profileId,
    });
    if (!result.ok) {
      return result.error === "suspended"
        ? fail(402, "suspended", "This workspace is suspended")
        : fail(403, "forbidden", "You do not hold this profile");
    }
    return json(200, {
      ok: true,
      profileId: result.profileId,
      version: result.version,
      entries: result.entries,
    });
  }

  if (leaf === "corrections" && request.method === "POST") {
    const body = await readJson(request);
    if (body === null) {
      return fail(400, "bad_request", "Body must be JSON");
    }
    const original = stringField(body, "original");
    const corrected = stringField(body, "corrected");
    if (original === null || corrected === null) {
      return fail(400, "invalid_term", "original and corrected are required");
    }
    const result = await ctx.runMutation(internal.profiles.recordCorrection, {
      userId: session.userId,
      profileId,
      original,
      corrected,
      now: Date.now(),
    });
    if (!result.ok) {
      const error = result.error;
      switch (error) {
        case "forbidden":
          return fail(403, error, "You do not hold this profile");
        case "suspended":
          return fail(402, error, "This workspace is suspended");
        case "invalid_term":
          return fail(400, error, "Correction is empty or unchanged");
        default: {
          const exhaustive: never = error;
          return exhaustive;
        }
      }
    }
    return json(200, { ok: true, version: result.version, count: result.count });
  }

  return fail(404, "not_found", "Unknown profile route");
});

// ── workspaces / seats (owner dashboard) ────────────────────────────────

const workspacesCollection = httpAction(async (ctx, request) => {
  const session = await requireSession(ctx, request);
  if (session instanceof Response) {
    return session;
  }
  if (request.method === "GET") {
    const workspaces = await ctx.runQuery(internal.workspaces.listForOwner, {
      userId: session.userId,
    });
    return json(200, { ok: true, workspaces });
  }
  // POST: in invite mode only existing owners may add workspaces.
  const body = await readJson(request);
  if (body === null) {
    return fail(400, "bad_request", "Body must be JSON");
  }
  if (signupMode() === "invite") {
    const owner = await ctx.runQuery(internal.workspaces.ownsAnyWorkspace, {
      userId: session.userId,
    });
    if (!owner) {
      return fail(403, "forbidden", "Only workspace owners can create workspaces");
    }
  }
  const result = await ctx.runMutation(internal.workspaces.create, {
    userId: session.userId,
    name: stringField(body, "name") ?? "",
    now: Date.now(),
  });
  if (!result.ok) {
    return fail(400, result.error, "Workspace name must be 1-80 characters");
  }
  return json(201, { ok: true, workspaceId: result.workspaceId });
});

const workspaceRoutes = httpAction(async (ctx, request) => {
  // /v1/workspaces/:id/seats (GET, POST) | /v1/workspaces/:id/seats/:seatId (DELETE)
  const segments = pathSegments(request);
  const workspaceId = segments[2] as Id<"workspaces"> | undefined;
  if (workspaceId === undefined || segments[3] !== "seats") {
    return fail(404, "not_found", "Unknown workspace route");
  }
  const session = await requireSession(ctx, request);
  if (session instanceof Response) {
    return session;
  }

  if (segments.length === 4 && request.method === "GET") {
    const result = await ctx.runQuery(internal.workspaces.listSeats, {
      userId: session.userId,
      workspaceId,
    });
    if (!result.ok) {
      return fail(403, "forbidden", "You do not own this workspace");
    }
    return json(200, { ok: true, workspace: result.workspace, seats: result.seats });
  }

  if (segments.length === 4 && request.method === "POST") {
    const body = await readJson(request);
    if (body === null) {
      return fail(400, "bad_request", "Body must be JSON");
    }
    const result = await ctx.runMutation(internal.workspaces.addSeat, {
      userId: session.userId,
      workspaceId,
      email: stringField(body, "email") ?? "",
      profileName: stringField(body, "profileName") ?? "",
      now: Date.now(),
    });
    if (!result.ok) {
      const error = result.error;
      switch (error) {
        case "forbidden":
          return fail(403, error, "You do not own this workspace");
        case "invalid_email":
          return fail(400, error, "Enter the artist's email");
        case "invalid_name":
          return fail(400, error, "Profile name must be 1-80 characters");
        case "duplicate_seat":
          return fail(409, error, "That email already has an active seat here");
        default: {
          const exhaustive: never = error;
          return exhaustive;
        }
      }
    }
    return json(201, { ok: true, seat: result.seat });
  }

  if (segments.length === 5 && request.method === "DELETE") {
    const seatId = segments[4] as Id<"profiles">;
    const result = await ctx.runMutation(internal.workspaces.revokeSeat, {
      userId: session.userId,
      workspaceId,
      seatId,
      now: Date.now(),
    });
    if (!result.ok) {
      return result.error === "forbidden"
        ? fail(403, result.error, "You do not own this workspace")
        : fail(404, result.error, "Seat not found in this workspace");
    }
    return json(200, { ok: true });
  }

  return fail(404, "not_found", "Unknown workspace route");
});

const health = httpAction(async () => {
  return json(200, { ok: true, service: "punch2pen-cloud", apiVersion: API_VERSION });
});

const preflight = httpAction(async () => {
  return new Response(null, { status: 204, headers: corsHeaders() });
});

const http = httpRouter();

http.route({ path: "/v1/health", method: "GET", handler: health });
http.route({ path: "/v1/auth/start", method: "POST", handler: authStart });
http.route({ path: "/v1/auth/verify", method: "POST", handler: authVerify });
http.route({ path: "/v1/auth/logout", method: "POST", handler: authLogout });
http.route({ path: "/v1/me", method: "GET", handler: me });
http.route({ pathPrefix: "/v1/profiles/", method: "GET", handler: profileRoutes });
http.route({ pathPrefix: "/v1/profiles/", method: "POST", handler: profileRoutes });
http.route({ path: "/v1/workspaces", method: "GET", handler: workspacesCollection });
http.route({ path: "/v1/workspaces", method: "POST", handler: workspacesCollection });
http.route({ pathPrefix: "/v1/workspaces/", method: "GET", handler: workspaceRoutes });
http.route({ pathPrefix: "/v1/workspaces/", method: "POST", handler: workspaceRoutes });
http.route({ pathPrefix: "/v1/workspaces/", method: "DELETE", handler: workspaceRoutes });
http.route({ pathPrefix: "/v1/", method: "OPTIONS", handler: preflight });
http.route({ path: "/v1/workspaces", method: "OPTIONS", handler: preflight });
http.route({ path: "/v1/me", method: "OPTIONS", handler: preflight });
http.route({ path: "/v1/health", method: "OPTIONS", handler: preflight });

export default http;
