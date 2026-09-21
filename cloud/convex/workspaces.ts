import { v } from "convex/values";
import type { Doc, Id } from "./_generated/dataModel";
import {
  internalMutation,
  internalQuery,
  type MutationCtx,
  type QueryCtx,
} from "./_generated/server";
import { normalizeEmail } from "./crypto";

const workspaceSummaryValidator = v.object({
  id: v.id("workspaces"),
  name: v.string(),
  status: v.union(v.literal("active"), v.literal("suspended")),
  seatCount: v.number(),
  createdAt: v.number(),
});

export const seatValidator = v.object({
  id: v.id("profiles"),
  profileName: v.string(),
  email: v.string(),
  claimed: v.boolean(),
  status: v.union(v.literal("active"), v.literal("revoked")),
  dictionaryVersion: v.number(),
  createdAt: v.number(),
});

function seatView(seat: Doc<"profiles">) {
  return {
    id: seat._id,
    profileName: seat.profileName,
    email: seat.email,
    claimed: seat.userId !== undefined,
    status: seat.status,
    dictionaryVersion: seat.dictionaryVersion,
    createdAt: seat.createdAt,
  };
}

async function requireOwner(
  ctx: QueryCtx | MutationCtx,
  workspaceId: Id<"workspaces">,
  userId: Id<"users">,
): Promise<Doc<"workspaces"> | null> {
  const workspace = await ctx.db.get(workspaceId);
  if (workspace === null || workspace.ownerUserId !== userId) {
    return null;
  }
  return workspace;
}

export const listForOwner = internalQuery({
  args: { userId: v.id("users") },
  returns: v.array(workspaceSummaryValidator),
  handler: async (ctx, args) => {
    const rows = await ctx.db
      .query("workspaces")
      .withIndex("by_owner", (q) => q.eq("ownerUserId", args.userId))
      .take(50);
    const out = [];
    for (const ws of rows) {
      const seats = await ctx.db
        .query("profiles")
        .withIndex("by_workspace", (q) => q.eq("workspaceId", ws._id))
        .take(500);
      out.push({
        id: ws._id,
        name: ws.name,
        status: ws.status,
        seatCount: seats.filter((s) => s.status === "active").length,
        createdAt: ws.createdAt,
      });
    }
    return out;
  },
});

export const ownsAnyWorkspace = internalQuery({
  args: { userId: v.id("users") },
  returns: v.boolean(),
  handler: async (ctx, args) => {
    const rows = await ctx.db
      .query("workspaces")
      .withIndex("by_owner", (q) => q.eq("ownerUserId", args.userId))
      .take(1);
    return rows.length > 0;
  },
});

export const create = internalMutation({
  args: { userId: v.id("users"), name: v.string(), now: v.number() },
  returns: v.union(
    v.object({ ok: v.literal(true), workspaceId: v.id("workspaces") }),
    v.object({ ok: v.literal(false), error: v.literal("invalid_name") }),
  ),
  handler: async (ctx, args) => {
    const name = args.name.trim();
    if (name.length === 0 || name.length > 80) {
      return { ok: false as const, error: "invalid_name" as const };
    }
    const workspaceId = await ctx.db.insert("workspaces", {
      name,
      ownerUserId: args.userId,
      status: "active",
      createdAt: args.now,
    });
    return { ok: true as const, workspaceId };
  },
});

export const listSeats = internalQuery({
  args: { userId: v.id("users"), workspaceId: v.id("workspaces") },
  returns: v.union(
    v.object({
      ok: v.literal(true),
      workspace: workspaceSummaryValidator,
      seats: v.array(seatValidator),
    }),
    v.object({ ok: v.literal(false), error: v.literal("forbidden") }),
  ),
  handler: async (ctx, args) => {
    const workspace = await requireOwner(ctx, args.workspaceId, args.userId);
    if (workspace === null) {
      return { ok: false as const, error: "forbidden" as const };
    }
    const seats = await ctx.db
      .query("profiles")
      .withIndex("by_workspace", (q) => q.eq("workspaceId", workspace._id))
      .take(500);
    return {
      ok: true as const,
      workspace: {
        id: workspace._id,
        name: workspace.name,
        status: workspace.status,
        seatCount: seats.filter((s) => s.status === "active").length,
        createdAt: workspace.createdAt,
      },
      seats: seats.map(seatView),
    };
  },
});

// The buyer creates a seat with the artist's email and a display name.
// The artist signs in to the plugin with that email; the seat binds to
// their user on first login. If the artist already has an account the
// seat binds immediately.
export const addSeat = internalMutation({
  args: {
    userId: v.id("users"),
    workspaceId: v.id("workspaces"),
    email: v.string(),
    profileName: v.string(),
    now: v.number(),
  },
  returns: v.union(
    v.object({ ok: v.literal(true), seat: seatValidator }),
    v.object({
      ok: v.literal(false),
      error: v.union(
        v.literal("forbidden"),
        v.literal("invalid_email"),
        v.literal("invalid_name"),
        v.literal("duplicate_seat"),
      ),
    }),
  ),
  handler: async (ctx, args) => {
    const workspace = await requireOwner(ctx, args.workspaceId, args.userId);
    if (workspace === null) {
      return { ok: false as const, error: "forbidden" as const };
    }
    const email = normalizeEmail(args.email);
    if (email === null) {
      return { ok: false as const, error: "invalid_email" as const };
    }
    const profileName = args.profileName.trim();
    if (profileName.length === 0 || profileName.length > 80) {
      return { ok: false as const, error: "invalid_name" as const };
    }
    const existing = await ctx.db
      .query("profiles")
      .withIndex("by_workspace", (q) => q.eq("workspaceId", workspace._id))
      .take(500);
    if (existing.some((s) => s.email === email && s.status === "active")) {
      return { ok: false as const, error: "duplicate_seat" as const };
    }
    const holder = await ctx.db
      .query("users")
      .withIndex("by_email", (q) => q.eq("email", email))
      .unique();
    const seatId = await ctx.db.insert("profiles", {
      workspaceId: workspace._id,
      profileName,
      email,
      userId: holder?._id,
      status: "active",
      dictionaryVersion: 0,
      createdAt: args.now,
    });
    const seat = await ctx.db.get(seatId);
    if (seat === null) {
      throw new Error("Seat insert did not return a row");
    }
    return { ok: true as const, seat: seatView(seat) };
  },
});

// Revoking keeps the dictionary rows (the artist's work) but the seat no
// longer appears in anyone's plugin and its dictionary cannot be read.
export const revokeSeat = internalMutation({
  args: {
    userId: v.id("users"),
    workspaceId: v.id("workspaces"),
    seatId: v.id("profiles"),
    now: v.number(),
  },
  returns: v.union(
    v.object({ ok: v.literal(true) }),
    v.object({
      ok: v.literal(false),
      error: v.union(v.literal("forbidden"), v.literal("not_found")),
    }),
  ),
  handler: async (ctx, args) => {
    const workspace = await requireOwner(ctx, args.workspaceId, args.userId);
    if (workspace === null) {
      return { ok: false as const, error: "forbidden" as const };
    }
    const seat = await ctx.db.get(args.seatId);
    if (seat === null || seat.workspaceId !== workspace._id) {
      return { ok: false as const, error: "not_found" as const };
    }
    if (seat.status === "active") {
      await ctx.db.patch(seat._id, { status: "revoked" });
    }
    return { ok: true as const };
  },
});
