// Deployment configuration read from Convex environment variables.
// Set with `npx convex env set NAME value`. Nothing here is a price or a
// vendor key baked into the plugin; the plugin never sees these.

export type SignupMode = "invite" | "open";

// invite (default): only emails that already hold a seat, or users an
// operator provisioned with admin:provisionOwner, can sign in.
// open: any email can sign in and gets a personal workspace of one.
export function signupMode(): SignupMode {
  return process.env.P2P_SIGNUP_MODE === "open" ? "open" : "invite";
}

export type MailProvider = "resend" | "echo" | "unconfigured";

// How login codes reach the user.
// resend: HTTPS call to Resend with RESEND_API_KEY + P2P_MAIL_FROM.
// echo:   code is returned in the API response. Local development only.
// unconfigured: auth/start refuses until an operator picks one.
export function mailProvider(): MailProvider {
  const raw = process.env.P2P_LOGIN_MAIL_PROVIDER;
  if (raw === "resend" || raw === "echo") {
    return raw;
  }
  return "unconfigured";
}

export function mailFrom(): string | undefined {
  return process.env.P2P_MAIL_FROM;
}

export function resendApiKey(): string | undefined {
  return process.env.RESEND_API_KEY;
}

// Origin allowed to call the HTTP API from a browser (the seat dashboard).
// Bearer tokens are not cookies, so "*" is acceptable for a first deploy.
export function dashboardOrigin(): string {
  return process.env.P2P_DASHBOARD_ORIGIN ?? "*";
}

export const LOGIN_CODE_TTL_MS = 10 * 60 * 1000;
export const LOGIN_CODE_MAX_ATTEMPTS = 5;
export const LOGIN_CHALLENGES_PER_WINDOW = 3;
export const LOGIN_CHALLENGE_WINDOW_MS = 10 * 60 * 1000;
export const SESSION_IDLE_TTL_MS = 90 * 24 * 60 * 60 * 1000;
// Storage cap per profile. The engine caps the whisper bias list far lower.
export const DICTIONARY_MAX_ENTRIES = 2000;
export const MAX_TERM_LENGTH = 120;
