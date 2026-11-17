"""Generate the Windows multi-resolution icon from the DEEP app-icon geometry."""

from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "resources" / "branding" / "deep-app-icon.ico"
SCALE = 16
SIZE = 64 * SCALE


def point(x: float, y: float) -> tuple[int, int]:
    return round(x * SCALE), round(y * SCALE)


image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
draw = ImageDraw.Draw(image)
draw.rounded_rectangle(
    (*point(3, 3), *point(61, 61)),
    radius=14 * SCALE,
    fill="#0d1724",
    outline="#2c5262",
    width=2 * SCALE,
)

mask = Image.new("L", image.size, 0)
mask_draw = ImageDraw.Draw(mask)
mask_draw.polygon(
    [point(14, 12), point(36, 12), point(50, 24), point(50, 40), point(36, 52), point(14, 52)],
    fill=255,
)
mask_draw.polygon(
    [point(25, 23), point(33, 23), point(39, 28), point(39, 36), point(33, 41), point(25, 41)],
    fill=0,
)

gradient = Image.new("RGBA", image.size)
gradient_draw = ImageDraw.Draw(gradient)
start = (120, 246, 220)
end = (40, 175, 196)
for x in range(SIZE):
    ratio = x / (SIZE - 1)
    color = tuple(round(a + (b - a) * ratio) for a, b in zip(start, end)) + (255,)
    gradient_draw.line((x, 0, x, SIZE), fill=color)
image.alpha_composite(Image.composite(gradient, Image.new("RGBA", image.size), mask))

draw = ImageDraw.Draw(image)
draw.line((*point(17, 16), *point(35, 16)), fill=(164, 255, 236, 160), width=max(1, round(1.5 * SCALE)))
draw.ellipse((*point(47, 10), *point(53, 16)), fill="#8affea")

OUTPUT.parent.mkdir(parents=True, exist_ok=True)
image.save(OUTPUT, format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print(OUTPUT)
