// Web Crypto helpers usable from both the Convex runtime and Node tests.

const encoder = new TextEncoder();

function toHex(bytes: Uint8Array): string {
  let out = "";
  for (const b of bytes) {
    out += b.toString(16).padStart(2, "0");
  }
  return out;
}

function toBase64Url(bytes: Uint8Array): string {
  let binary = "";
  for (const b of bytes) {
    binary += String.fromCharCode(b);
  }
  return btoa(binary).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export async function sha256Hex(input: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", encoder.encode(input));
  return toHex(new Uint8Array(digest));
}

export function randomHex(byteLength: number): string {
  const bytes = new Uint8Array(byteLength);
  crypto.getRandomValues(bytes);
  return toHex(bytes);
}

// Session tokens: 32 random bytes, base64url, prefixed so they are easy to
// spot in logs and easy to revoke by pattern. Only the SHA-256 is stored.
export function randomSessionToken(): string {
  const bytes = new Uint8Array(32);
  crypto.getRandomValues(bytes);
  return `p2p_${toBase64Url(bytes)}`;
}

// Six-digit login code drawn with rejection sampling so every value in
// 000000..999999 is equally likely.
export function randomLoginCode(): string {
  const limit = 1_000_000;
  const maxUnbiased = Math.floor(0x1_0000_0000 / limit) * limit;
  const buf = new Uint32Array(1);
  for (;;) {
    crypto.getRandomValues(buf);
    const value = buf[0] ?? 0;
    if (value < maxUnbiased) {
      return String(value % limit).padStart(6, "0");
    }
  }
}

export async function hashLoginCode(code: string, salt: string): Promise<string> {
  return sha256Hex(`${salt}:${code}`);
}

export function constantTimeEqual(a: string, b: string): boolean {
  if (a.length !== b.length) {
    return false;
  }
  let diff = 0;
  for (let i = 0; i < a.length; i++) {
    diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  }
  return diff === 0;
}

export function normalizeEmail(raw: string): string | null {
  const email = raw.trim().toLowerCase();
  // Deliberately loose: one @, something on both sides, no whitespace.
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email) || email.length > 254) {
    return null;
  }
  return email;
}
