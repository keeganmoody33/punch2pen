import { v } from "convex/values";
import type { Doc, Id } from "./_generated/dataModel";
import {
  internalMutation,
  internalQuery,
  type MutationCtx,
  type QueryCtx,
} from "./_generated/server";
import { constantTimeEqual } from "./crypto";
import {
  LOGIN_CHALLENGES_PER_WINDOW,
  LOGIN_CHALLENGE_WINDOW_MS,
  LOGIN_CODE_MAX_ATTEMPTS,
  LOGIN_CODE_TTL_MS,
  SESSION_IDLE_TTL_MS,
} from "./env";

const signupModeValidator = v.union(v.literal("invite"), v.literal("open"));

export const sessionInfoValidator = v.object({
  sessionId: v.id("sessions"),
  userId: v.id("users"),
  email: v.string(),
  lastSeenAt: v.number(),
});
export type SessionInfo = {
  sessionId: Id<"sessions">;
  userId: Id<"users">;
  email: string;
  lastSeenAt: number;
};

async function userByEmail(
  ctx: QueryCtx | MutationCtx,
  email: string,
): Promise<Doc<"users"> | null> {
  return await ctx.db
    .query("users")
    .withIndex("by_email", (q) => q.eq("email", email))
    .unique();
}

async function hasActiveSeatForEmail(
  ctx: QueryCtx | MutationCtx,
  email: string,
): Promise<boolean> {
  const seats = await ctx.db
    .query("profiles")
    .withIndex("by_email", (q) => q.eq("email", email))
    .take(50);
  return seats.some((seat) => seat.status === "active");
}

// Invite mode: an email may start a login only if it already exists as a
// user (operator-provisioned owner) or holds an active seat somewhere.
export const emailAllowed = internalQuery({
  args: { email: v.string(), signupMode: signupModeValidator },
  returns: v.boolean(),
  handler: async (ctx, args) => {
    if (args.signupMode === "open") {
      return true;
    }
    if ((await userByEmail(ctx, args.email)) !== null) {
      return true;
    }
    return await hasActiveSeatForEmail(ctx, args.email);
  },
});

export const createChallenge = internalMutation({
  args: {
    email: v.string(),
    codeHash: v.string(),
    salt: v.string(),
    now: v.number(),
  },
  returns: v.union(
    v.object({ ok: v.literal(true), challengeId: v.id("loginChallenges") }),
    v.object({ ok: v.literal(false), error: v.literal("rate_limited") }),
  ),
  handler: async (ctx, args) => {
    const recent = await ctx.db
      .query("loginChallenges")
      .withIndex("by_email", (q) => q.eq("email", args.email))
      .order("desc")
      .take(LOGIN_CHALLENGES_PER_WINDOW);
    const liveInWindow = recent.filter(
      (c) =>
        c.consumedAt === undefined &&
        args.now - c.createdAt < LOGIN_CHALLENGE_WINDOW_MS,
    );
    if (liveInWindow.length >= LOGIN_CHALLENGES_PER_WINDOW) {
      return { ok: false as const, error: "rate_limited" as const };
    }
    const challengeId = await ctx.db.insert("loginChallenges", {
      email: args.email,
      codeHash: args.codeHash,
      salt: args.salt,
      expiresAt: args.now + LOGIN_CODE_TTL_MS,
      attempts: 0,
      createdAt: args.now,
    });
    return { ok: true as const, challengeId };
  },
});

// The action needs the salt before it can hash the submitted code.
export const latestOpenChallenge = internalQuery({
  args: { email: v.string(), now: v.number() },
  returns: v.union(
    v.object({ challengeId: v.id("loginChallenges"), salt: v.string() }),
    v.null(),
  ),
  handler: async (ctx, args) => {
    const recent = await ctx.db
      .query("loginChallenges")
      .withIndex("by_email", (q) => q.eq("email", args.email))
      .order("desc")
      .take(LOGIN_CHALLENGES_PER_WINDOW);
    const open = recent.find(
      (c) =>
        c.consumedAt === undefined &&
        c.expiresAt > args.now &&
        c.attempts < LOGIN_CODE_MAX_ATTEMPTS,
    );
    return open ? { challengeId: open._id, salt: open.salt } : null;
  },
});

const verifyResultValidator = v.union(
  v.object({ ok: v.literal(true), userId: v.id("users"), email: v.string() }),
  v.object({
    ok: v.literal(false),
    error: v.union(
      v.literal("code_invalid"),
      v.literal("code_expired"),
      v.literal("too_many_attempts"),
      v.literal("not_invited"),
    ),
  }),
);

async function bindSeatsToUser(
  ctx: MutationCtx,
  email: string,
  userId: Id<"users">,
): Promise<number> {
  const seats = await ctx.db
    .query("profiles")
    .withIndex("by_email", (q) => q.eq("email", email))
    .take(50);
  let bound = 0;
  for (const seat of seats) {
    if (seat.userId === undefined && seat.status === "active") {
      await ctx.db.patch(seat._id, { userId });
      bound += 1;
    }
  }
  return bound;
}

async function countProfilesForUser(
  ctx: MutationCtx,
  userId: Id<"users">,
): Promise<number> {
  const rows = await ctx.db
    .query("profiles")
    .withIndex("by_user", (q) => q.eq("userId", userId))
    .take(1);
  return rows.length;
}

export async function createPersonalWorkspace(
  ctx: MutationCtx,
  userId: Id<"users">,
  email: string,
  now: number,
): Promise<Id<"profiles">> {
  const localPart = email.split("@")[0] ?? "artist";
  const workspaceId = await ctx.db.insert("workspaces", {
    name: localPart,
    ownerUserId: userId,
    status: "active",
    createdAt: now,
  });
  return await ctx.db.insert("profiles", {
    workspaceId,
    profileName: localPart,
    email,
    userId,
    status: "active",
    dictionaryVersion: 0,
    createdAt: now,
  });
}

// Consumes the challenge and mints a session in one transaction so a code
// can never be redeemed twice.
export const verifyChallenge = internalMutation({
  args: {
    challengeId: v.id("loginChallenges"),
    email: v.string(),
    codeHash: v.string(),
    tokenHash: v.string(),
    deviceName: v.string(),
    platform: v.string(),
    signupMode: signupModeValidator,
    now: v.number(),
  },
  returns: verifyResultValidator,
  handler: async (ctx, args) => {
    const challenge = await ctx.db.get(args.challengeId);
    if (
      challenge === null ||
      challenge.email !== args.email ||
      challenge.consumedAt !== undefined
    ) {
      return { ok: false as const, error: "code_invalid" as const };
    }
    if (challenge.expiresAt <= args.now) {
      return { ok: false as const, error: "code_expired" as const };
    }
    if (challenge.attempts >= LOGIN_CODE_MAX_ATTEMPTS) {
      return { ok: false as const, error: "too_many_attempts" as const };
    }
    if (!constantTimeEqual(challenge.codeHash, args.codeHash)) {
      await ctx.db.patch(challenge._id, { attempts: challenge.attempts + 1 });
      return {
        ok: false as const,
        error:
          challenge.attempts + 1 >= LOGIN_CODE_MAX_ATTEMPTS
            ? ("too_many_attempts" as const)
            : ("code_invalid" as const),
      };
    }

    let user = await userByEmail(ctx, args.email);
    if (user === null) {
      const invited =
        args.signupMode === "open" ||
        (await hasActiveSeatForEmail(ctx, args.email));
      if (!invited) {
        return { ok: false as const, error: "not_invited" as const };
      }
      const userId = await ctx.db.insert("users", {
        email: args.email,
        createdAt: args.now,
        lastLoginAt: args.now,
      });
      user = await ctx.db.get(userId);
      if (user === null) {
        throw new Error("User insert did not return a row");
      }
    } else {
      await ctx.db.patch(user._id, { lastLoginAt: args.now });
    }

    await ctx.db.patch(challenge._id, { consumedAt: args.now });
    await bindSeatsToUser(ctx, args.email, user._id);

    if (
      args.signupMode === "open" &&
      (await countProfilesForUser(ctx, user._id)) === 0
    ) {
      await createPersonalWorkspace(ctx, user._id, args.email, args.now);
    }

    await ctx.db.insert("sessions", {
      userId: user._id,
      tokenHash: args.tokenHash,
      deviceName: args.deviceName,
      platform: args.platform,
      createdAt: args.now,
      lastSeenAt: args.now,
    });

    return { ok: true as const, userId: user._id, email: user.email };
  },
});

export const sessionForToken = internalQuery({
  args: { tokenHash: v.string(), now: v.number() },
  returns: v.union(sessionInfoValidator, v.null()),
  handler: async (ctx, args) => {
    const session = await ctx.db
      .query("sessions")
      .withIndex("by_token_hash", (q) => q.eq("tokenHash", args.tokenHash))
      .unique();
    if (
      session === null ||
      session.revokedAt !== undefined ||
      args.now - session.lastSeenAt > SESSION_IDLE_TTL_MS
    ) {
      return null;
    }
    const user = await ctx.db.get(session.userId);
    if (user === null) {
      return null;
    }
    return {
      sessionId: session._id,
      userId: user._id,
      email: user.email,
      lastSeenAt: session.lastSeenAt,
    };
  },
});

// Called at most once an hour per session so reads stay cheap.
export const touchSession = internalMutation({
  args: { sessionId: v.id("sessions"), now: v.number() },
  returns: v.null(),
  handler: async (ctx, args) => {
    const session = await ctx.db.get(args.sessionId);
    if (session !== null && session.revokedAt === undefined) {
      await ctx.db.patch(session._id, { lastSeenAt: args.now });
    }
    return null;
  },
});

export const revokeSession = internalMutation({
  args: { sessionId: v.id("sessions"), now: v.number() },
  returns: v.null(),
  handler: async (ctx, args) => {
    const session = await ctx.db.get(args.sessionId);
    if (session !== null && session.revokedAt === undefined) {
      await ctx.db.patch(session._id, { revokedAt: args.now });
    }
    return null;
  },
});
