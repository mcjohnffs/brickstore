#!/usr/bin/env python3
"""BrickStore DK-11204 print service (v4).

Uses the proven brother_ql path:
BrotherQLRaster -> conversion.convert -> backends.helpers.send.

v4 adds a cleaner default label preset while preserving user overrides, exact PNG
preview, robust index fitting, price rounding, and suppression of non-useful
BrickLink colour placeholders such as "(Not Applicable)".
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

LEGACY_DEFAULTS: dict[str, Any] = {
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

CLEAN_DEFAULTS: dict[str, Any] = {
    **LEGACY_DEFAULTS,
    "font_item_id": 32,
    "font_index": 18,
    "font_price": 32,
    "font_name": 23,
    "font_meta": 18,
    "font_extra": 16,
    "x_index": 470,
    "y_index": 108,
    "index_max_width": 72,
    "y_item_id": 4,
    "y_price": 4,
    "y_name": 50,
    "y_meta": 108,
    "y_extra": 132,
    "y_separator": 156,
    "margin_left": 14,
    "margin_right": 14,
    "top_split": 285,
    "separator_width": 1,
}


def _same(a: Any, b: Any) -> bool:
    if isinstance(b, float):
        try:
            return abs(float(a) - b) < 1e-9
        except (TypeError, ValueError):
            return False
    return a == b


def merged_settings(incoming: Any) -> dict[str, Any]:
    """Upgrade untouched legacy defaults while preserving actual user edits."""
    result = dict(CLEAN_DEFAULTS)
    if not isinstance(incoming, dict):
        return result
    for key, clean_value in CLEAN_DEFAULTS.items():
        if key not in incoming:
            continue
        value = incoming[key]
        legacy = LEGACY_DEFAULTS.get(key, clean_value)
        result[key] = clean_value if _same(value, legacy) else value
    return result


def _bool(settings: dict[str, Any], key: str) -> bool:
    return bool(settings.get(key, CLEAN_DEFAULTS[key]))


def _int(settings: dict[str, Any], key: str, minimum: int, maximum: int) -> int:
    try:
        value = int(settings.get(key, CLEAN_DEFAULTS[key]))
    except (TypeError, ValueError):
        value = int(CLEAN_DEFAULTS[key])
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
                  top_limit: int, bottom_limit: int,
                  align: str = "left") -> tuple[int, int, int, int]:
    max_width = max(20, min(max_width, right_limit - left_limit))
    max_height = max(8, min(max_height, bottom_limit - top_limit))
    font = fit_font(draw, text, max_width, start_size, 6, bold, max_height)
    width, height, box = text_size(draw, text, font)
    desired_x = x - width if align == "right" else x
    bx = min(max(desired_x, left_limit), max(left_limit, right_limit - width))
    by = min(max(y, top_limit), max(top_limit, bottom_limit - height))
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


def useful_color(value: Any) -> str:
    text = str(value or "").strip()
    normalized = text.lower().strip("()[]{} ")
    if normalized in {
        "", "not applicable", "n/a", "na", "none", "no color", "no colour",
        "not_applicable", "not-applicable"
    }:
        return ""
    return text


def render_label(data: dict[str, Any], settings_in: Any = None) -> Image.Image:
    s = merged_settings(settings_in)
    item_id = str(data.get("item_id", "")).strip()
    index = str(data.get("index", "")).strip()
    name = str(data.get("name", "")).strip()
    color = useful_color(data.get("color", ""))
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
        draw_text_box(draw, item_id, left, _int(s, "y_item_id", 0, 164),
                      max(20, split - left - 10), 40,
                      _int(s, "font_item_id", 6, 100), _bool(s, "bold_item_id"),
                      left, split - 5, 0, 48)

    if _bool(s, "show_price"):
        price = format_price(data, s)
        draw_text_box(draw, price, right, _int(s, "y_price", 0, 164),
                      max(20, right - split - 5), 40,
                      _int(s, "font_price", 6, 100), _bool(s, "bold_price"),
                      split + 5, right, 0, 48, align="right")

    if _bool(s, "show_name") and name:
        draw_text_box(draw, name, left, _int(s, "y_name", 0, 164),
                      max(20, right - left), 40,
                      _int(s, "font_name", 6, 100), _bool(s, "bold_name"),
                      left, right, 44, 98)

    index_left_reserve = right
    if _bool(s, "show_index") and index:
        prefix = str(s.get("index_prefix", "#"))
        index_text = f"{prefix}{index}"
        max_width = _int(s, "index_max_width", 24, max(24, right - left))
        desired_right = min(right, max(left + max_width, _int(s, "x_index", 0, 565) + max_width))
        bbox = draw_text_box(draw, index_text, desired_right, _int(s, "y_index", 0, 164),
                             max_width, max(8, separator_y - 100),
                             _int(s, "font_index", 6, 100), _bool(s, "bold_index"),
                             left, right, 98, max(99, separator_y - 2), align="right")
        index_left_reserve = max(left, bbox[0] - 12)

    meta_parts: list[str] = []
    if _bool(s, "show_color") and color:
        meta_parts.append(color)
    if _bool(s, "show_condition") and condition:
        meta_parts.append(condition)
    meta = "  ·  ".join(meta_parts)
    if meta:
        meta_right = index_left_reserve if index_left_reserve < right else right
        draw_text_box(draw, meta, left, _int(s, "y_meta", 0, 164),
                      max(20, meta_right - left), max(8, separator_y - 100),
                      _int(s, "font_meta", 6, 100), _bool(s, "bold_meta"),
                      left, max(left + 20, meta_right), 98, max(99, separator_y - 2))

    extra_parts: list[str] = []
    if _bool(s, "show_quantity") and quantity not in (None, ""):
        extra_parts.append(f"Qty {quantity}")
    if _bool(s, "show_remarks") and remarks:
        extra_parts.append(remarks)
    if _bool(s, "show_comments") and comments:
        extra_parts.append(comments)
    extra = "  ·  ".join(extra_parts)
    if extra:
        extra_right = index_left_reserve if index_left_reserve < right else right
        draw_text_box(draw, extra, left, _int(s, "y_extra", 0, 164),
                      max(20, extra_right - left), max(8, separator_y - 118),
                      _int(s, "font_extra", 6, 100), _bool(s, "bold_extra"),
                      left, max(left + 20, extra_right), 118, max(119, separator_y - 2))

    if _bool(s, "show_separator"):
        width = _int(s, "separator_width", 1, 8)
        draw.line((left, separator_y, right, separator_y), fill="black", width=width)

    return image


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
    server_version = "BrickStoreBrotherQL/4.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        LOG.info("%s - %s", self.address_string(), fmt % args)

    def _json(self, status: int, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def _read_json(self) -> dict[str, Any]:
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0 or length > 1024 * 1024:
            raise ValueError("Invalid request size")
        payload = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(payload, dict):
            raise ValueError("JSON body must be an object")
        return payload

    def do_GET(self) -> None:
        if self.path.rstrip("/") == "/health":
            p = self.server.label_printer
            self._json(200, {
                "ok": True,
                "version": 4,
                "preview": True,
                "clean_default": True,
                "model": p.model,
                "printer": p.printer,
                "backend": p.backend,
                "label": LABEL_ID,
            })
            return
        self._json(404, {"ok": False, "error": "Not found"})

    def do_POST(self) -> None:
        try:
            if self.path.rstrip("/") == "/preview":
                payload = self._read_json()
                label = payload.get("label", {})
                if not isinstance(label, dict):
                    raise ValueError("label must be an object")
                image = render_label(label, payload.get("settings", {}))
                buf = io.BytesIO()
                image.save(buf, "PNG")
                raw = buf.getvalue()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(raw)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(raw)
                return

            if self.path.rstrip("/") != "/print":
                self._json(404, {"ok": False, "error": "Not found"})
                return

            payload = self._read_json()
            settings = payload.get("settings", {})
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

            printed = self.server.label_printer.print_labels(labels, settings)
            self._json(200, {"ok": True, "printed": printed, "version": 4, "label": LABEL_ID})
        except Exception as exc:
            LOG.exception("Request failed")
            self._json(500, {"ok": False, "error": str(exc)})


class LabelServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], label_printer: LabelPrinter):
        super().__init__(address, Handler)
        self.label_printer = label_printer


def main() -> int:
    parser = argparse.ArgumentParser(description="BrickStore Brother QL label service v4")
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

    label_printer = LabelPrinter(args.printer or "dry-run", args.model, args.backend, args.dry_run)
    server = LabelServer((args.host, args.port), label_printer)
    LOG.info("Brother QL service v4 listening on http://%s:%d", args.host, args.port)
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
