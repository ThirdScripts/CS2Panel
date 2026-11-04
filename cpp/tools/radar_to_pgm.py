"""Convert a radar capture into the walkable raster that cs2n_navgen eats.

The CS2 radar paints floor space as mid-grey, walls and props as dark or very
bright pixels, so the luminance band [min, max] is exactly what the builder
classifies as walkable. Requires Pillow or opencv-python (dev-time only; the
shipped binary never needs it).

Usage:
  python tools/radar_to_pgm.py <radar.png> <out.pgm> [--min 32] [--max 125]
"""
import argparse
import sys

try:
    from PIL import Image
except ImportError:
    Image = None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--min", dest="min_value", type=int, default=32)
    parser.add_argument("--max", dest="max_value", type=int, default=125)
    parser.add_argument("--size", type=int, default=0, help="resize to this square size")
    args = parser.parse_args()

    if Image is None:
        print("Pillow is required: pip install pillow", file=sys.stderr)
        return 2

    image = Image.open(args.input).convert("L")
    if args.size > 0:
        image = image.resize((args.size, args.size), Image.NEAREST)

    pixels = list(image.tobytes())
    walkable = sum(1 for p in pixels if args.min_value <= p <= args.max_value)
    coverage = walkable / max(1, len(pixels))
    print(f"size={image.width}x{image.height} walkable_coverage={coverage:.3f}")
    if coverage < 0.05 or coverage > 0.95:
        print("warning: coverage outside 5..95% -- check the luminance band", file=sys.stderr)

    with open(args.output, "wb") as handle:
        handle.write(f"P5\n{image.width} {image.height}\n255\n".encode("ascii"))
        handle.write(bytes(pixels))
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
