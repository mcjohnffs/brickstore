#!/usr/bin/env python3
"""BrickStore DK-11204 print service (v3).

Uses the proven brother_ql path:
BrotherQLRaster -> conversion.convert -> backends.helpers.send.

v3 adds an exact PNG preview endpoint and robust index-number fitting/clamping.
"""

from __future__ import annotations

import argparse
import io
import json
import logging
import os
from decimal import Decimal, ROUND_CEILING, ROUND_FLOOR, ROUND_HALF_UP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont
from brother_ql.conversion import convert
from brother_ql.backends.helpers import send
from brother_ql.raster import BrotherQLRaster

LOG = logging.getLogger("brickstore-brother-ql")
LABEL_ID = "17x54"
LABEL_PIXELS = (566, 165)
DEFAULT_MODEL = "QL-810W"
DEFAULT_BACKEND = "network"

DEFAULT_SETTINGS: dict[str, Any] = {
    "show_item_id": True,
    "show_index": True,
    "show_name": True,
    "show_color": True,
    "show_condition": True,
    "show_price": True,
    "show_currency": True,
    "show_quantity": False,
    "show_remarks": False,
    "show_comments": False,
    "show_separator": True,
    "font_item_id": 39,
    "font_index": 19,
    "font_price": 39,
    "font_name": 29,
    "font_meta": 24,
    "font_extra": 19,
    "bold_item_id": True,
    "bold_index": True,
    "bold_price": True,
    "bold_name": False,
    "bold_meta": False,
    "bold_extra": False,
    "x_index": 430,
    "y_index": 126,
    "index_max_width": 120,
    "index_prefix": "#",
    "y_item_id": 4,
    "y_price": 4,
    "y_name": 57,
    "y_meta": 105,
    "y_extra": 132,
    "y_separator": 158,
    "margin_left": 12,
    "margin_right": 12,
    "top_split": 275,
    "separator_width": 2,
    "rounding_mode": "none",
    "rounding_step": 0.05,
    "price_decimals": 2,
    "currency_before": True,
    "threshold": 70.0,
    "dither": False,
    "hq": True,
    "cut": True,
}


def merged_settings(incoming: Any) -> dict[str, Any]:
    result = dict(DEFAULT_SETTINGS)
    if isinstance(incoming, dict):
        for key in result:
            if key in incoming:
                result[key] = incoming[key]
    return result


def _bool(settings: dict[str, Any], key: str) -> bool:
    return bool(settings.get(key, DEFAULT_SETTINGS[key]))


def _int(settings: dict[str, Any], key: str, minimum: int, maximum: int) -> int:
    try:
        value = int(settings.get(key, DEFAULT_SETTINGS[key]))
    except (TypeError, ValueError):
        value = int(DEFAULT_SETTINGS[key])
    return max(minimum, min(value, maximum))


def _font_candidates(bold: bool) -> list[str]:
    if os.name == "nt":
        root = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
        names = ["arialbd.ttf", "segoeuib.ttf"] if bold else ["arial.ttf", "segoeui.ttf"]
        return [str(root / name) for name in names]
    return [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold else
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    ]


def load_font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    size = max(6, min(int(size), 100))
    for candidate in _font_candidates(bold):
        if Path(candidate).is_file():
            try:
                return ImageFont.truetype(candidate, size)
            except OSError:
                pass
    try:
        return ImageFont.truetype("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf", size)
    except OSError:
        return ImageFont.load_default()


def text_size(draw: ImageDraw.ImageDraw, text: str, font: ImageFont.ImageFont) -> tuple[int, int, tuple[int, int, int, int]]:
    box = draw.textbbox((0, 0), text, font=font)
    return box[2] - box[0], box[3] - box[1], box


def fit_font(draw: ImageDraw.ImageDraw, text: str, max_width: int,
             start_size: int, min_size: int = 6, bold: bool = False,
             max_height: int | None = None) -> ImageFont.ImageFont:
    start_size = max(min_size, min(int(start_size), 100))
    for size in range(start_size, min_size - 1, -1):
        font = load_font(size, bold)
        width, height, _ = text_size(draw, text, font)
        if width <= max_width and (max_height is None or height <= max_height):
            return font
    return load_font(min_size, bold)


def draw_text_box(draw: ImageDraw.ImageDraw, text: str, x: int, y: int,
                  max_width: int, max_height: int, start_size: int,
                  bold: bool, left_limit: int, right_limit: int,
                  top_limit: int, bottom_limit: int) -> tuple[int, int, int, int]:
    """Draw text fully inside a bounded rectangle and return its final bbox.

    x/y represent the desired top-left of the visible text bounding box. The font
    automatically shrinks, and the visible bbox is clamped into the printable area.
    """
    max_width = max(20, min(max_width, right_limit - left_limit))
    max_height = max(8, min(max_height, bottom_limit - top_limit))
    font = fit_font(draw, text, max_width, start_size, 6, bold, max_height)
    width, height, box = text_size(draw, text, font)

    bx = min(max(x, left_limit), max(left_limit, right_limit - width))
    by = min(max(y, top_limit), max(top_limit, bottom_limit - height))

    # Pillow's text anchor includes font ascender offsets. Shift by bbox origin so
    # the *visible* text begins at bx/by and cannot be clipped.
    draw.text((bx - box[0], by - box[1]), text, fill="black", font=font)
    return bx, by, bx + width, by + height


def round_price(value: float, settings: dict[str, Any]) -> Decimal:
    price = Decimal(str(value))
    mode = str(settings.get("rounding_mode", "none")).lower()
    try:
        step = Decimal(str(settings.get("rounding_step", 0.05)))
    except Exception:
        step = Decimal("0.05")
    if step <= 0 or mode == "none":
        return price
    quotient = price / step
    if mode == "up":
        units = quotient.to_integral_value(rounding=ROUND_CEILING)
    elif mode == "down":
        units = quotient.to_integral_value(rounding=ROUND_FLOOR)
    else:
        units = quotient.to_integral_value(rounding=ROUND_HALF_UP)
    return units * step


def format_price(data: dict[str, Any], settings: dict[str, Any]) -> str:
    try:
        raw = float(data.get("price", 0.0))
    except (TypeError, ValueError):
        raw = 0.0
    rounded = round_price(raw, settings)
    decimals = _int(settings, "price_decimals", 0, 4)
    number = f"{rounded:.{decimals}f}"
    if not _bool(settings, "show_currency"):
        return number
    currency = str(data.get("currency", "CHF")).strip() or "CHF"
    return f"{currency} {number}" if _bool(settings, "currency_before") else f"{number} {currency}"


def render_label(data: dict[str, Any], settings_in: Any = None) -> Image.Image:
    s = merged_settings(settings_in)
    item_id = str(data.get("item_id", "")).strip()
    index = str(data.get("index", "")).strip()
    name = str(data.get("name", "")).strip()
    color = str(data.get("color", "No color")).strip()
    condition = str(data.get("condition", "New")).strip()
    remarks = str(data.get("remarks", "")).strip()
    comments = str(data.get("comments", "")).strip()
    quantity = data.get("quantity", "")

    image = Image.new("RGB", LABEL_PIXELS, "white")
    draw = ImageDraw.Draw(image)
    left = _int(s, "margin_left", 0, 100)
    right = LABEL_PIXELS[0] - _int(s, "margin_right", 0, 100)
    split = _int(s, "top_split", left + 40, right - 40)
    separator_y = _int(s, "y_separator", 0, 164)

    if _bool(s, "show_item_id") and item_id:
        font = fit_font(draw, item_id, max(20, split - left - 5),
                        _int(s, "font_item_id", 6, 100), 6, _bool(s, "bold_item_id"))
        draw.text((left, _int(s, "y_item_id", 0, 164)), item_id, fill="black", font=font)

    if _bool(s, "show_price"):
        price = format_price(data, s)
        font = fit_font(draw, price, max(20, right - split - 5),
                        _int(s, "font_price", 6, 100), 6, _bool(s, "bold_price"))
        box = draw.textbbox((0, 0), price, font=font)
        draw.text((right - (box[2] - box[0]), _int(s, "y_price", 0, 164)),
                  price, fill="black", font=font)

    if _bool(s, "show_name") and name:
        font = fit_font(draw, name, max(20, right - left),
                        _int(s, "font_name", 6, 100), 6, _bool(s, "bold_name"))
        draw.text((left, _int(s, "y_name", 0, 164)), name, fill="black", font=font)

    meta_parts: list[str] = []
    if _bool(s, "show_color") and color:
        meta_parts.append(color)
    if _bool(s, "show_condition") and condition:
        meta_parts.append(condition)
    meta = "  ·  ".join(meta_parts)
    if meta:
        font = fit_font(draw, meta, max(20, right - left),
                        _int(s, "font_meta", 6, 100), 6, _bool(s, "bold_meta"))
        draw.text((left, _int(s, "y_meta", 0, 164)), meta, fill="black", font=font)

    extra_parts: list[str] = []
    if _bool(s, "show_quantity") and quantity not in (None, ""):
        extra_parts.append(f"Qty {quantity}")
    if _bool(s, "show_remarks") and remarks:
        extra_parts.append(remarks)
    if _bool(s, "show_comments") and comments:
        extra_parts.append(comments)
    extra = "  ·  ".join(extra_parts)
    if extra:
        # Leave room on the right for Index when it shares the bottom row.
        extra_right = right
        if _bool(s, "show_index") and index:
            ix = _int(s, "x_index", left, right - 20)
            if ix > left + 40:
                extra_right = min(extra_right, ix - 8)
        font = fit_font(draw, extra, max(20, extra_right - left),
                        _int(s, "font_extra", 6, 100), 6, _bool(s, "bold_extra"))
        draw.text((left, _int(s, "y_extra", 0, 164)), extra, fill="black", font=font)

    if _bool(s, "show_index") and index:
        prefix = str(s.get("index_prefix", "#"))
        text = f"{prefix}{index}"
        x = _int(s, "x_index", left, right - 20)
        y = _int(s, "y_index", 0, 164)
        requested_width = _int(s, "index_max_width", 30, LABEL_PIXELS[0])
        max_width = min(requested_width, max(20, right - x))
        bottom_limit = separator_y - 3 if _bool(s, "show_separator") else LABEL_PIXELS[1] - 2
        bottom_limit = max(10, min(bottom_limit, LABEL_PIXELS[1] - 1))
        max_height = max(8, bottom_limit - max(0, min(y, bottom_limit - 8)))
        final_box = draw_text_box(
            draw, text, x, y, max_width, max_height,
            _int(s, "font_index", 6, 100), _bool(s, "bold_index"),
            left, right, 0, bottom_limit,
        )
        LOG.debug("Index '%s' rendered in bbox %s (requested x=%s y=%s width=%s)",
                  text, final_box, x, y, requested_width)

    if _bool(s, "show_separator"):
        width = _int(s, "separator_width", 1, 8)
        draw.line((left, separator_y, right, separator_y), fill="black", width=width)

    return image


def png_bytes(image: Image.Image) -> bytes:
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


class LabelPrinter:
    def __init__(self, printer: str, model: str, backend: str, dry_run: bool = False):
        self.printer = printer
        self.model = model
        self.backend = backend
        self.dry_run = dry_run

    def print_labels(self, payloads: list[dict[str, Any]], settings: Any = None) -> int:
        s = merged_settings(settings)
        images: list[Image.Image] = []
        for payload in payloads:
            copies = max(1, min(int(payload.get("copies", 1)), 250))
            image = render_label(payload, s)
            images.extend(image.copy() for _ in range(copies))
        if not images:
            raise ValueError("No labels to print")
        if self.dry_run:
            for n, image in enumerate(images, 1):
                image.save(f"brickstore-label-preview-{n:03d}.png")
            return len(images)
        qlr = BrotherQLRaster(self.model)
        qlr.exception_on_warning = True
        instructions = convert(
            qlr=qlr,
            images=images,
            label=LABEL_ID,
            rotate="auto",
            threshold=float(s.get("threshold", 70.0)),
            dither=_bool(s, "dither"),
            compress=False,
            red=False,
            dpi_600=False,
            hq=_bool(s, "hq"),
            cut=_bool(s, "cut"),
        )
        send(
            instructions=instructions,
            printer_identifier=self.printer,
            backend_identifier=self.backend,
            blocking=True,
        )
        return len(images)


class Handler(BaseHTTPRequestHandler):
    server_version = "BrickStoreBrotherQL/3.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        LOG.info("%s - %s", self.address_string(), fmt % args)

    def _json(self, status: int, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _png(self, image: Image.Image) -> None:
        raw = png_bytes(image)
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        if self.path.rstrip("/") == "/health":
            p = self.server.label_printer
            self._json(200, {
                "ok": True,
                "version": 3,
                "preview": True,
                "model": p.model,
                "printer": p.printer,
                "backend": p.backend,
                "label": LABEL_ID,
            })
            return
        self._json(404, {"ok": False, "error": "Not found"})

    def _read_payload(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 1024 * 1024:
            raise ValueError("Invalid request size")
        payload = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("JSON body must be an object")
        return payload

    def do_POST(self) -> None:
        try:
            payload = self._read_payload()
            settings = payload.get("settings", {})

            if self.path.rstrip("/") == "/preview":
                sample = payload.get("label") if isinstance(payload.get("label"), dict) else payload
                if not str(sample.get("item_id", "")).strip():
                    raise ValueError("item_id is required")
                LOG.info("Preview item=%s index=%r price=%r", sample.get("item_id"), sample.get("index"), sample.get("price"))
                self._png(render_label(sample, settings))
                return

            if self.path.rstrip("/") != "/print":
                self._json(404, {"ok": False, "error": "Not found"})
                return

            labels = payload.get("labels")
            if isinstance(labels, list):
                labels = [entry for entry in labels if isinstance(entry, dict)]
            else:
                labels = [payload]
            if not labels:
                raise ValueError("No labels supplied")
            for entry in labels:
                if not str(entry.get("item_id", "")).strip():
                    raise ValueError("item_id is required for every label")
                LOG.info("Print item=%s index=%r price=%r", entry.get("item_id"), entry.get("index"), entry.get("price"))
            printed = self.server.label_printer.print_labels(labels, settings)
            self._json(200, {"ok": True, "printed": printed, "version": 3, "label": LABEL_ID})
        except Exception as exc:
            LOG.exception("Request failed")
            self._json(500, {"ok": False, "error": str(exc)})


class LabelServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], label_printer: LabelPrinter):
        super().__init__(address, Handler)
        self.label_printer = label_printer


def main() -> int:
    parser = argparse.ArgumentParser(description="BrickStore Brother QL label service v3")
    parser.add_argument("--printer", required=not ("BROTHER_QL_PRINTER" in os.environ),
                        default=os.environ.get("BROTHER_QL_PRINTER"),
                        help="Printer identifier, e.g. tcp://192.168.1.7")
    parser.add_argument("--model", default=os.environ.get("BROTHER_QL_MODEL", DEFAULT_MODEL))
    parser.add_argument("--backend", default=os.environ.get("BROTHER_QL_BACKEND", DEFAULT_BACKEND))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8013)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--log-level", default="INFO")
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(message)s",
    )
    printer = LabelPrinter(args.printer or "dry-run", args.model, args.backend, args.dry_run)
    server = LabelServer((args.host, args.port), printer)
    LOG.info("Brother QL service v3 listening on http://%s:%d", args.host, args.port)
    LOG.info("Printer: %s | Backend: %s | Model: %s | Label: %s",
             args.printer, args.backend, args.model, LABEL_ID)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
