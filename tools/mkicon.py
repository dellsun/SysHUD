import struct

W = H = 32
px = bytearray()
for y in range(H):
    for x in range(W):
        cx, cy = W / 2 - 0.5, H / 2 - 0.5
        d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
        if d < W / 2 - 2:
            px += bytes([90, 160, 230, 255])   # B,G,R,A 蓝色
        else:
            px += bytes([0, 0, 0, 0])

rows = [px[i * W * 4:(i + 1) * W * 4] for i in range(H)]
px_rev = b''.join(rows[::-1])

hdr = struct.pack('<IiiHHIIiiII', 40, W, H * 2, 1, 32, 0, len(px_rev), 0, 0, 0, 0)
andmask = bytes(((W + 31) // 32 * 4) * H)
img = hdr + px_rev + andmask

icondir = struct.pack('<HHH', 0, 1, 1)
entry = struct.pack('<BBBBHHII', W, H, 0, 0, 1, 32, len(img), 6 + 16)
with open(r'C:\Users\q123\Desktop\SysHUD\res\app.ico', 'wb') as f:
    f.write(icondir + entry + img)
print('ICO_WRITTEN', len(icondir + entry + img))
