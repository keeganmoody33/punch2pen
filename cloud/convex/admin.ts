import { v } from "convex/values";
import { internalMutation } from "./_generated/server";
import { createPersonalWorkspace } from "./auth";
import { normalizeEmail } from "./crypto";

// Operator-only. In invite mode this is how the first studio buyer or solo
// pro gets in before any billing exists:
//
//   npx convex run admin:provisionOwner '{"email":"buyer@studio.com","workspaceName":"Studio X"}'
//
// Creates the user, a workspace they own, and a seat for themselves so the
// plugin shows an active profile on first login.
export const provisionOwner = internalMutation({
  args: {
    email: v.string(),
    workspaceName: v.optional(v.string()),
  },
  returns: v.object({
    userId: v.id("users"),
    profileId: v.id("profiles"),
    created: v.boolean(),
  }),
  handler: async (ctx, args) => {
    const email = normalizeEmail(args.email);
    if (email === null) {
      throw new Error("Invalid email address");
    }
    const now = Date.now();
    const existing = await ctx.db
      .query("users")
      .withIndex("by_email", (q) => q.eq("email", email))
      .unique();
    if (existing !== null) {
      const seats = await ctx.db
        .query("profiles")
        .withIndex("by_user", (q) => q.eq("userId", existing._id))
        .take(1);
      const seat = seats[0];
      if (seat !== undefined) {
        return { userId: existing._id, profileId: seat._id, created: false };
      }
      const profileId = await createPersonalWorkspace(
        ctx,
        existing._id,
        email,
        now,
      );
      if (args.workspaceName !== undefined) {
        const profile = await ctx.db.get(profileId);
        if (profile !== null) {
          await ctx.db.patch(profile.workspaceId, {
            name: args.workspaceName.trim(),
          });
        }
      }
      return { userId: existing._id, profileId, created: false };
    }

    const userId = await ctx.db.insert("users", { email, createdAt: now });
    const profileId = await createPersonalWorkspace(ctx, userId, email, now);
    if (args.workspaceName !== undefined) {
      const profile = await ctx.db.get(profileId);
      if (profile !== null) {
        await ctx.db.patch(profile.workspaceId, {
          name: args.workspaceName.trim(),
        });
      }
    }
    return { userId, profileId, created: true };
  },
});

// Entitlement switch. No prices live here; an operator flips a workspace
// when a buyer stops paying (whatever "paying" ends up meaning).
export const setWorkspaceStatus = internalMutation({
  args: {
    workspaceId: v.id("workspaces"),
    status: v.union(v.literal("active"), v.literal("suspended")),
  },
  returns: v.null(),
  handler: async (ctx, args) => {
    const workspace = await ctx.db.get(args.workspaceId);
    if (workspace === null) {
      throw new Error("Workspace not found");
    }
    await ctx.db.patch(workspace._id, { status: args.status });
    return null;
  },
});
