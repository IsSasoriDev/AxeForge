#pragma once
#include <stddef.h>

// Optional two-factor login for the web API (TOTP, RFC 6238: 6 digits, 30 s,
// SHA-1, as Google Authenticator, Aegis, 1Password etc. expect). When enabled,
// every request that changes something needs a session from a valid code.
namespace otp {
void load();
bool enabled();

// Starts setup: a new secret, kept pending until confirm() sees a valid code.
// Writes the base32 secret and the otpauth:// URI.
void begin_setup(char *secret_b32, size_t slen, char *uri, size_t ulen);
// The codes below return nullptr on success, else why it failed.
const char *confirm(const char *code, char *token, size_t tlen);   // enables, logs in
const char *login(const char *code, char *token, size_t tlen);
const char *disable(const char *code);
void logout(const char *token);
bool session_ok(const char *token);
}
