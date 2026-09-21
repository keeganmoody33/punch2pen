import { v } from "convex/values";
import type { Doc, Id } from "./_generated/dataModel";
import {
  internalMutation,
  internalQuery,
  type MutationCtx,
  type QueryCtx,
} from "./_generated/server";
import { DICTIONARY_MAX_ENTRIES, MAX_TERM_LENGTH } from "./env";

export const profileSummaryValidator = v.object({
  id: v.id("profiles"),
  name: v.string(),
  workspaceId: v.id("workspaces"),
  workspaceName: v.string(),
  role: v.union(v.literal("owner"), v.literal("artist")),
  status: v.union(v.literal("active"), v.literal("suspended")),
  dictionaryVersion: v.number(),
});

export const dictionaryEntryValidator = v.object({
  original: v.string(),
  corrected: v.string(),
  count: v.number(),
  updatedAt: v.number(),
});

async function summarize(
  ctx: QueryCtx,
  profile: Doc<"profiles">,
  userId: Id<"users">,
) {
  const workspace = await ctx.db.get(profile.workspaceId);
  if (workspace === null) {
    return null;
  }
  return {
    id: profile._id,
    name: profile.profileName,
    workspaceId: workspace._id,
    workspaceName: workspace.name,
    role:
      workspace.ownerUserId === userId ? ("owner" as const) : ("artist" as const),
    status:
      workspace.status === "active" ? ("active" as const) : ("suspended" as const),
    dictionaryVersion: profile.dictionaryVersion,
  };
}

// Profiles the signed-in user can act as inside the plugin. Owners do not
// see seats they merely manage: acting as an artist means holding the seat.
export const listForUser = internalQuery({
  args: { userId: v.id("users") },
  returns: v.array(profileSummaryValidator),
  handler: async (ctx, args) => {
    const rows = await ctx.db
      .query("profiles")
      .withIndex("by_user", (q) => q.eq("userId", args.userId))
      .take(100);
    const out = [];
    for (const row of rows) {
      if (row.status !== "active") {
        continue;
      }
      const summary = await summarize(ctx, row, args.userId);
      if (summary !== null) {
        out.push(summary);
      }
    }
    return out;
  },
});

type Access =
  | { ok: true; profile: Doc<"profiles"> }
  | { ok: false; error: "forbidden" | "suspended" };

// Isolation rule: only the seat holder touches that seat's dictionary.
async function requireHolder(
  ctx: QueryCtx | MutationCtx,
  profileId: Id<"profiles">,
  userId: Id<"users">,
): Promise<Access> {
  const profile = await ctx.db.get(profileId);
  if (
    profile === null ||
    profile.userId !== userId ||
    profile.status !== "active"
  ) {
    return { ok: false, error: "forbidden" };
  }
  const workspace = await ctx.db.get(profile.workspaceId);
  if (workspace === null) {
    return { ok: false, error: "forbidden" };
  }
  if (workspace.status !== "active") {
    return { ok: false, error: "suspended" };
  }
  return { ok: true, profile };
}

export const getDictionary = internalQuery({
  args: { userId: v.id("users"), profileId: v.id("profiles") },
  returns: v.union(
    v.object({
      ok: v.literal(true),
      profileId: v.id("profiles"),
      version: v.number(),
      entries: v.array(dictionaryEntryValidator),
    }),
    v.object({
      ok: v.literal(false),
      error: v.union(v.literal("forbidden"), v.literal("suspended")),
    }),
  ),
  handler: async (ctx, args) => {
    const access = await requireHolder(ctx, args.profileId, args.userId);
    if (!access.ok) {
      return { ok: false as const, error: access.error };
    }
    const rows = await ctx.db
      .query("dictionaryEntries")
      .withIndex("by_profile", (q) => q.eq("profileId", args.profileId))
      .take(DICTIONARY_MAX_ENTRIES);
    return {
      ok: true as const,
      profileId: access.profile._id,
      version: access.profile.dictionaryVersion,
      entries: rows.map((r) => ({
        original: r.original,
        corrected: r.corrected,
        count: r.count,
        updatedAt: r.updatedAt,
      })),
    };
  },
});

function cleanTerm(raw: string): string | null {
  const term = raw.trim();
  if (term.length === 0 || term.length > MAX_TERM_LENGTH) {
    return null;
  }
  return term;
}

// Upsert by (profile, original). Case-sensitive on purpose: the engine maps
// the raw transcript word by exact match before anything summarises it.
export const recordCorrection = internalMutation({
  args: {
    userId: v.id("users"),
    profileId: v.id("profiles"),
    original: v.string(),
    corrected: v.string(),
    now: v.number(),
  },
  returns: v.union(
    v.object({
      ok: v.literal(true),
      version: v.number(),
      count: v.number(),
    }),
    v.object({
      ok: v.literal(false),
      error: v.union(
        v.literal("forbidden"),
        v.literal("suspended"),
        v.literal("invalid_term"),
      ),
    }),
  ),
  handler: async (ctx, args) => {
    const access = await requireHolder(ctx, args.profileId, args.userId);
    if (!access.ok) {
      return { ok: false as const, error: access.error };
    }
    const original = cleanTerm(args.original);
    const corrected = cleanTerm(args.corrected);
    if (original === null || corrected === null || original === corrected) {
      return { ok: false as const, error: "invalid_term" as const };
    }

    const existing = await ctx.db
      .query("dictionaryEntries")
      .withIndex("by_profile_original", (q) =>
        q.eq("profileId", args.profileId).eq("original", original),
      )
      .unique();

    let count = 1;
    if (existing !== null) {
      count = existing.count + 1;
      await ctx.db.patch(existing._id, {
        corrected,
        count,
        updatedAt: args.now,
      });
    } else {
      const sample = await ctx.db
        .query("dictionaryEntries")
        .withIndex("by_profile", (q) => q.eq("profileId", args.profileId))
        .take(DICTIONARY_MAX_ENTRIES);
      if (sample.length >= DICTIONARY_MAX_ENTRIES) {
        const oldest = await ctx.db
          .query("dictionaryEntries")
          .withIndex("by_profile_updated", (q) =>
            q.eq("profileId", args.profileId),
          )
          .order("asc")
          .first();
        if (oldest !== null) {
          await ctx.db.delete(oldest._id);
        }
      }
      await ctx.db.insert("dictionaryEntries", {
        profileId: args.profileId,
        original,
        corrected,
        count,
        updatedAt: args.now,
      });
    }

    const version = access.profile.dictionaryVersion + 1;
    await ctx.db.patch(access.profile._id, { dictionaryVersion: version });
    return { ok: true as const, version, count };
  },
});
