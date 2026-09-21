import { mailFrom, mailProvider, resendApiKey, type MailProvider } from "./env";

export type Delivery =
  | { ok: true; provider: "resend" }
  | { ok: true; provider: "echo"; code: string }
  | { ok: false; error: "delivery_unconfigured" | "delivery_failed" };

// One place to swap the email provider. The plugin never learns which one
// is in use; it only sees `delivery: "email"` or, in dev, the echoed code.
export async function deliverLoginCode(
  email: string,
  code: string,
  provider: MailProvider = mailProvider(),
): Promise<Delivery> {
  switch (provider) {
    case "echo":
      return { ok: true, provider: "echo", code };
    case "resend": {
      const apiKey = resendApiKey();
      const from = mailFrom();
      if (apiKey === undefined || from === undefined) {
        return { ok: false, error: "delivery_unconfigured" };
      }
      const response = await fetch("https://api.resend.com/emails", {
        method: "POST",
        headers: {
          Authorization: `Bearer ${apiKey}`,
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          from,
          to: [email],
          subject: `${code} is your punch2pen sign-in code`,
          text:
            `Your punch2pen sign-in code is ${code}.\n\n` +
            "Enter it in the plugin within 10 minutes. " +
            "If you did not request this, ignore this email.",
        }),
      });
      if (!response.ok) {
        console.error("Login code delivery failed", response.status);
        return { ok: false, error: "delivery_failed" };
      }
      return { ok: true, provider: "resend" };
    }
    case "unconfigured":
      return { ok: false, error: "delivery_unconfigured" };
    default: {
      const exhaustive: never = provider;
      return exhaustive;
    }
  }
}
