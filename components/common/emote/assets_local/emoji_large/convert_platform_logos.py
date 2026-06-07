#!/usr/bin/env python3
import argparse
import io
import os
import struct
from pathlib import Path

from PIL import Image, ImageOps, ImageDraw, ImageFont

try:
    import cairosvg
except Exception:
    cairosvg = None

MAGIC = 0x19
CF_RGB565A8 = 0x0A
ICON_SIZE = 72


def load_image(path: Path) -> Image.Image:
    suffix = path.suffix.lower()
    if suffix == ".svg":
        if cairosvg is None:
            raise RuntimeError(f"cairosvg is unavailable, cannot convert {path.name}")
        png_data = cairosvg.svg2png(url=str(path), output_width=ICON_SIZE, output_height=ICON_SIZE)
        return Image.open(io.BytesIO(png_data)).convert("RGBA")
    return Image.open(path).convert("RGBA")


def fit_icon(img: Image.Image, size: int = ICON_SIZE) -> Image.Image:
    icon = ImageOps.contain(img, (size, size), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    x = (size - icon.width) // 2
    y = (size - icon.height) // 2
    canvas.paste(icon, (x, y), icon)
    return canvas


def to_gray(img: Image.Image) -> Image.Image:
    alpha = img.getchannel("A")
    gray = ImageOps.grayscale(img.convert("RGB"))
    rgba = Image.merge("RGBA", (gray, gray, gray, alpha))
    return rgba


def to_rgb565a8_bin(img: Image.Image, out_path: Path, rgb565_endian: str = "be") -> None:
    width, height = img.size
    stride = width * 2

    header_word = (MAGIC & 0xFF) | ((CF_RGB565A8 & 0xFF) << 8)
    header = struct.pack("<IHHHH", header_word, width, height, stride, 0)

    rgb565 = bytearray()
    alpha = bytearray()
    pack_fmt = ">H" if rgb565_endian == "be" else "<H"
    for r, g, b, a in img.getdata():
        # Keep straight-alpha RGB to avoid double-darkening in render pipelines
        # that already apply alpha blending using the A8 plane.
        if a == 0:
            r = g = b = 0
        value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        rgb565.extend(struct.pack(pack_fmt, value))
        alpha.append(a)

    out_path.write_bytes(header + rgb565 + alpha)


def convert_pair(src_path: Path, color_name: str, gray_name: str, out_dir: Path, rgb565_endian: str) -> None:
    base = fit_icon(load_image(src_path))
    gray = to_gray(base)

    to_rgb565a8_bin(base, out_dir / color_name, rgb565_endian)
    if gray_name != color_name:
        to_rgb565a8_bin(gray, out_dir / gray_name, rgb565_endian)


def build_synthetic_qq_icon() -> Image.Image:
    img = Image.new("RGBA", (ICON_SIZE, ICON_SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.ellipse((1, 1, ICON_SIZE - 2, ICON_SIZE - 2), fill=(18, 132, 255, 255))
    draw.ellipse((3, 3, ICON_SIZE - 4, ICON_SIZE - 4), outline=(255, 255, 255, 90), width=1)

    text = "QQ"
    try:
        font = ImageFont.truetype("arial.ttf", 11)
    except Exception:
        font = ImageFont.load_default()
    box = draw.textbbox((0, 0), text, font=font)
    tw = box[2] - box[0]
    th = box[3] - box[1]
    draw.text(((ICON_SIZE - tw) // 2, (ICON_SIZE - th) // 2 - 1), text, fill=(255, 255, 255, 255), font=font)
    return img


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert platform logos into emote RGB565A8 bins")
    parser.add_argument(
        "--rgb565-endian",
        choices=["be", "le"],
        default="be",
        help="RGB565 payload byte order (default: be, which is correct for current emote assets)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[5]
    static_dir = repo_root / "application" / "edge_agent" / "fatfs_image" / "static"
    out_dir = Path(__file__).resolve().parent

    mappings = [
        ("ESP-Claw.png", "c_claw.bin", "c_claw.bin"),
        ("openai.png", "l_oa_c.bin", "l_oa_g.bin"),
        ("aliyunbailian.png", "l_bl_c.bin", "l_bl_g.bin"),
        ("deepseek-color.png", "l_ds_c.bin", "l_ds_g.bin"),
        ("anthropic.png", "l_an_c.bin", "l_an_g.bin"),
        ("wechat.png", "i_wc_c.bin", "i_wc_g.bin"),
        ("bytedance-color.png", "i_fs_c.bin", "i_fs_g.bin"),
        ("telegram.png", "i_tg_c.bin", "i_tg_g.bin"),
        ("QQ.png", "i_qq_c.bin", "i_qq_g.bin"),
    ]

    failed = []
    for src, color_out, gray_out in mappings:
        src_path = static_dir / src
        try:
            if not src_path.exists():
                raise FileNotFoundError(str(src_path))
            convert_pair(src_path, color_out, gray_out, out_dir, args.rgb565_endian)
            print(f"[OK] {src} -> {color_out}, {gray_out}")
        except Exception as e:
            if src == "QQ.png":
                qq = build_synthetic_qq_icon()
                to_rgb565a8_bin(qq, out_dir / color_out, args.rgb565_endian)
                to_rgb565a8_bin(to_gray(qq), out_dir / gray_out, args.rgb565_endian)
                print(f"[OK] {src} -> synthetic QQ icon fallback")
            else:
                failed.append((src, str(e)))
                print(f"[FAIL] {src}: {e}")

    if failed:
        raise SystemExit("Logo conversion failed for some files")
