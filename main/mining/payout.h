#pragma once
#include <stddef.h>
#include <stdint.h>

// Solo-mining payout check. A solo pool builds the coinbase so the block reward
// goes to the address in your username; this reads the coinbase outputs back as
// addresses so the miner can show who a found block would pay.
// Mainnet, testnet and regtest; P2PKH, P2SH, P2WPKH, P2WSH and P2TR.
namespace payout {

constexpr size_t MAX_SCRIPT = 42;  // witness v0..16 program up to 40 bytes + 2
constexpr size_t MAX_ADDR = 91;    // bech32 limit + NUL

enum class Net : uint8_t { Main, Test, Regtest };

// Decodes an address (checksum included) to its output script. Returns the
// script length, or 0 if `addr` isn't a valid address.
size_t address_to_script(const char *addr, uint8_t *script, size_t cap, Net *net = nullptr);

// Encodes an output script as an address of `net`. Data outputs give
// "OP_RETURN"; other non-standard scripts give false and "".
bool script_to_address(const uint8_t *script, size_t len, Net net, char *out, size_t cap);

// The address part of a pool username: "bc1q...xyz.rig1" -> "bc1q...xyz".
// False if that part isn't a valid address (a pooled account name, say).
bool user_address(const char *user, char *addr, size_t cap, Net *net = nullptr);

} // namespace payout
