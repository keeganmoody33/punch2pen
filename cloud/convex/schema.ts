import { defineSchema, defineTable } from "convex/server";
import { v } from "convex/values";

// A profile is one artist's portable dictionary. A workspace owns one or
// more profiles ("seats"). A solo pro user is a workspace of one. Prices,
// plans, and billing vendors are deliberately absent: `status` is the only
// entitlement switch and it is flipped by an operator, not by this code.
export const workspaceStatus = v.union(
  v.literal("active"),
  v.literal("suspended"),
);

export const profileStatus = v.union(
  v.literal("active"),
  v.literal("revoked"),
);

export default defineSchema({
  users: defineTable({
    email: v.string(),
    createdAt: v.number(),
    lastLoginAt: v.optional(v.number()),
  }).index("by_email", ["email"]),

  workspaces: defineTable({
    name: v.string(),
    ownerUserId: v.id("users"),
    status: workspaceStatus,
    createdAt: v.number(),
  }).index("by_owner", ["ownerUserId"]),

  profiles: defineTable({
    workspaceId: v.id("workspaces"),
    profileName: v.string(),
    // Lower-cased invitee email. Bound to a user row on first login.
    email: v.string(),
    userId: v.optional(v.id("users")),
    status: profileStatus,
    dictionaryVersion: v.number(),
    createdAt: v.number(),
  })
    .index("by_workspace", ["workspaceId"])
    .index("by_email", ["email"])
    .index("by_user", ["userId"]),

  dictionaryEntries: defineTable({
    profileId: v.id("profiles"),
    // Case-sensitive: "hell nah" and "Hell Nah" are two entries.
    original: v.string(),
    corrected: v.string(),
    count: v.number(),
    updatedAt: v.number(),
  })
    .index("by_profile", ["profileId"])
    .index("by_profile_original", ["profileId", "original"])
    .index("by_profile_updated", ["profileId", "updatedAt"]),

  loginChallenges: defineTable({
    email: v.string(),
    codeHash: v.string(),
    salt: v.string(),
    expiresAt: v.number(),
    attempts: v.number(),
    createdAt: v.number(),
    consumedAt: v.optional(v.number()),
  }).index("by_email", ["email"]),

  sessions: defineTable({
    userId: v.id("users"),
    tokenHash: v.string(),
    deviceName: v.string(),
    platform: v.string(),
    createdAt: v.number(),
    lastSeenAt: v.number(),
    revokedAt: v.optional(v.number()),
  })
    .index("by_token_hash", ["tokenHash"])
    .index("by_user", ["userId"]),
});
