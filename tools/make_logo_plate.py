#!/usr/bin/env python3
"""Builds branding/tremu-logo-plate.png: the logo on a brushed metal plate, for the carousel's top left corner."""
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

BRANDING = Path(__file__).resolve().parent.parent / 'branding'
WIDTH, HEIGHT = 340, 116   # final size, also the plate's place in the 1280x720 carousel
PLATE_W, PLATE_H, CUT = 330, 104, 34
LOGO_W, LOGO_X, LOGO_Y = 280, 26, 26
SCALE = 3                  # drawn larger, then reduced for clean edges


def plate_mask(inset=0.0):
    """The plate's outline: square along the screen's corner, its free corner cut like the background's panels."""
    w, h, cut, i = PLATE_W * SCALE, PLATE_H * SCALE, CUT * SCALE, inset * SCALE
    points = [(-10, -10), (w - i, -10), (w - i, h - cut - i * .41), (w - cut - i * .41, h - i), (-10, h - i)]
    mask = Image.new('L', (WIDTH * SCALE, HEIGHT * SCALE), 0)
    ImageDraw.Draw(mask).polygon(points, fill=255)
    return mask


def brushed_metal():
    w, h = WIDTH * SCALE, HEIGHT * SCALE
    y = np.linspace(0, 1, h)[:, None]
    x = np.linspace(0, 1, w)[None, :]
    # Vertical gradient: light catches the upper edge, the middle stays dark so the silver letters stand out.
    base = np.interp(y, [0, .10, .42, .78, 1], [86, 58, 30, 38, 54])
    # Soft diagonal reflections, as on a polished panel.
    diagonal = x * w - y * h * 1.1
    sheen = 24 * np.exp(-((diagonal - w * .22) / (w * .10)) ** 2) + 15 * np.exp(-((diagonal - w * .78) / (w * .07)) ** 2)
    # Fine horizontal brushing: noise stretched along the rows.
    rng = np.random.default_rng(7)
    noise = rng.normal(0, 1, (h, w))
    kernel = np.ones(121) / 121
    noise = np.apply_along_axis(lambda row: np.convolve(row, kernel, mode='same'), 1, noise)
    brushed = noise / noise.std() * 5.5
    grey = np.clip(base + sheen + brushed, 0, 255)
    # A cold steel tint.
    rgb = np.stack([grey * .97, grey * .99, grey * 1.04], axis=2).clip(0, 255).astype('uint8')
    return Image.fromarray(rgb, 'RGB')


def logo():
    source = Image.open(BRANDING / 'tremu-logo.png').convert('RGBA')
    source = source.crop(source.getbbox())
    height = round(LOGO_W * SCALE * source.height / source.width)
    return source.resize((LOGO_W * SCALE, height), Image.LANCZOS)


def main():
    size = (WIDTH * SCALE, HEIGHT * SCALE)
    out = Image.new('RGBA', size, (0, 0, 0, 0))

    # Shadow on the background, so the plate sits above it.
    shadow = plate_mask().filter(ImageFilter.GaussianBlur(5 * SCALE)).point(lambda v: v * 200 // 255)
    out.paste((0, 0, 0, 255), (0, 0), shadow)

    # Red light from under the plate, like the background's seams.
    seam = ImageChops.subtract(plate_mask(-1.6), plate_mask())
    out.paste((235, 22, 32, 255), (0, 0), seam.filter(ImageFilter.GaussianBlur(.4 * SCALE)))

    metal = brushed_metal().convert('RGBA')
    out.paste(metal, (0, 0), plate_mask())

    # Bevel: a bright rim, a dark groove inside it.
    rim = ImageChops.subtract(plate_mask(), plate_mask(1.6))
    out.paste((206, 210, 218, 255), (0, 0), rim)
    groove = ImageChops.subtract(plate_mask(1.6), plate_mask(3.4))
    out.paste((10, 10, 12, 255), (0, 0), groove.point(lambda v: v * 170 // 255))

    # The logo, with a shadow pressing it onto the metal.
    letters = logo()
    place = (LOGO_X * SCALE, LOGO_Y * SCALE)
    alpha = Image.new('L', size, 0)
    alpha.paste(letters.getchannel('A'), place)
    drop = ImageChops.offset(alpha, SCALE, 2 * SCALE).filter(ImageFilter.GaussianBlur(1.5 * SCALE))
    out.paste((0, 0, 0, 255), (0, 0), drop.point(lambda v: v * 190 // 255))
    out.alpha_composite(letters, place)

    out = out.resize((WIDTH, HEIGHT), Image.LANCZOS)
    out.save(BRANDING / 'tremu-logo-plate.png', optimize=True)
    print('ok', out.size)


if __name__ == '__main__':
    main()
