#!/usr/bin/env python3
"""Draws Rigcard's icon (res/rigcard.ico, res/rigcard.png, web/icon.png): a gradient tile with a little build card on it."""
from PIL import Image, ImageDraw, ImageFilter
import os

def tile(size):
    s = size * 4  # supersample
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    # gradient background, violet -> blue
    grad = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(grad)
    a, b = (124, 92, 255), (52, 140, 255)
    for y in range(s):
        for_t = y / (s - 1)
        c = tuple(int(a[i] + (b[i] - a[i]) * for_t) for i in range(3)) + (255,)
        gd.line([(0, y), (s, y)], fill=c)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.23), fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    # the card
    m = s * 0.18
    card = [m, s * 0.24, s - m, s * 0.76]
    shadow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle([card[0], card[1] + s * 0.03, card[2], card[3] + s * 0.03], radius=int(s * 0.07), fill=(20, 10, 60, 110))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(s * 0.02)))
    d.rounded_rectangle(card, radius=int(s * 0.07), fill=(18, 20, 34, 255))
    # photo block
    d.rounded_rectangle([card[0] + s * 0.05, card[1] + s * 0.05, card[0] + s * 0.29, card[3] - s * 0.05], radius=int(s * 0.035), fill=(108, 140, 255, 255))
    # text lines
    x0 = card[0] + s * 0.34
    for i, (w, col) in enumerate([(0.30, (244, 246, 251)), (0.22, (154, 163, 184)), (0.26, (154, 163, 184))]):
        y = card[1] + s * (0.08 + i * 0.105)
        d.rounded_rectangle([x0, y, x0 + s * w, y + s * 0.05], radius=int(s * 0.025), fill=col + (255,))
    # accent dot (the wordmark's dot)
    d.ellipse([card[2] - s * 0.11, card[3] - s * 0.11, card[2] - s * 0.05, card[3] - s * 0.05], fill=(176, 108, 255, 255))
    return img.resize((size, size), Image.LANCZOS)

root = os.path.join(os.path.dirname(__file__), "..")
big = tile(256)
big.save(os.path.join(root, "res", "rigcard.png"))
os.makedirs(os.path.join(root, "web"), exist_ok=True)
tile(512).save(os.path.join(root, "web", "icon.png"))
big.save(os.path.join(root, "res", "rigcard.ico"), sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)])
print("icons written")
