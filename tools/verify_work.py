# Mirrors axeforge/main/mining/work.cpp byte-for-byte and checks it against block 400000.
import hashlib, json, struct, urllib.request
API = "https://blockstream.info/api"
get = lambda p: urllib.request.urlopen(API + p).read().decode()
dsha = lambda b: hashlib.sha256(hashlib.sha256(b).digest()).digest()
swap_words = lambda b: b"".join(b[i:i+4][::-1] for i in range(0, len(b), 4))

h = get("/block-height/400000").strip()
hdr = bytes.fromhex(get(f"/block/{h}/header"))
txids = json.loads(get(f"/block/{h}/txids"))
coinbase_hex = get(f"/tx/{txids[0]}/hex")

# stratum-style merkle branches (internal byte order)
layer = [bytes.fromhex(t)[::-1] for t in txids]
# recompute properly: branch at each level is sibling of the coinbase path
branches = []
while len(layer) > 1:
    if len(layer) % 2: layer.append(layer[-1])
    branches.append(layer[1])
    layer = [dsha(layer[i] + layer[i+1]) for i in range(0, len(layer), 2)]

version, = struct.unpack("<I", hdr[0:4]); prev = hdr[4:36]
ntime, nbits, nonce = struct.unpack("<III", hdr[68:80])
stratum_prev_raw = swap_words(prev)            # what the pool sends (hex2bin'd)

# --- work::build ---
pair = dsha(bytes.fromhex(coinbase_hex))
for b in branches: pair = dsha(pair + b)
merkle_root = pair
w_prev = swap_words(stratum_prev_raw)
chip_merkle = swap_words(merkle_root)[::-1]
chip_prev = stratum_prev_raw[::-1]

# --- work::nonce_difficulty ---
header = struct.pack("<I", version) + w_prev + merkle_root + struct.pack("<III", ntime, nbits, nonce)
hv = int.from_bytes(dsha(header), "little")
diff = 0x00000000FFFF0000000000000000000000000000000000000000000000000000 / hv
exp, mant = nbits >> 24, nbits & 0xffffff
net = 0x00000000FFFF0000000000000000000000000000000000000000000000000000 / (mant * 256 ** (exp - 3))

print("txs", len(txids), "branches", len(branches))
print("merkle root matches header:", merkle_root == hdr[36:68])
print("rebuilt header == real header:", header == hdr)
print(f"share diff {diff:.4g} >= network diff {net:.4g}:", diff >= net)
# block height parse (work::block_height_from_coinb1)
push = int(coinbase_hex[84:86], 16); hgt = int.from_bytes(bytes.fromhex(coinbase_hex[86:86+2*push]), "little")
print("height from coinbase:", hgt)
