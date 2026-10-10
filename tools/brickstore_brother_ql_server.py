#!/usr/bin/env python3
"""BrickStore DK-11204 print service using brother_ql directly.

This bypasses the operating system printer driver and sends Brother raster data
straight to a Brother QL printer. It is intentionally tiny and only exposes the
endpoints BrickStore needs.

Typical network-printer usage:
    py -m pip install brother_ql Pillow
    py tools/brickstore_brother_ql_server.py --printer tcp://192.168.1.50:9100

Then BrickStore can POST labels to http://127.0.0.1:8013/print.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont
from brother_ql import BrotherQLRaster, create_label
from brother_ql.backends import backend_factory, guess_backend

LOG = logging.getLogger("brickstore-brother-ql")
LABEL_ID = "17x54"          # Brother DK-11204
LABEL_PIXELS = (566, 165)   # printable landscape canvas used by brother_ql


def _font_candidates(bold: bool) -> list[str]:
    if os.name == "nt":
        win = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
        names = ["arialbd.ttf", "segoeuib.ttf"] if bold else ["arial.ttf", "segoeui.ttf"]
        return [str(win / name) for name in names]
    names = (
        [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
        ]
        if bold
        else [
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        ]
    )
    return names


def load_font(size: int, bold: bool = False) -> ImageFont.ImageFont:
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


def fit_font(draw: ImageDraw.ImageDraw, text: str, max_width: int,
             start_size: int, min_size: int, bold: bool = False) -> ImageFont.ImageFont:
    for size in range(start_size, min_size - 1, -1):
        font = load_font(size, bold)
        box = draw.textbbox((0, 0), text, font=font)
        if box[2] - box[0] <= max_width:
            return font
    return load_font(min_size, bold)


def render_label(data: dict[str, Any]) -> Image.Image:
    item_id = str(data.get("item_id", "")).strip()
    name = str(data.get("name", "")).strip()
    color = str(data.get("color", "No color")).strip()
    condition = str(data.get("condition", "New")).strip()
    currency = str(data.get("currency", "CHF")).strip() or "CHF"
    try:
        price_value = float(data.get("price", 0.0))
    except (TypeError, ValueError):
        price_value = 0.0

    price = f"{currency} {price_value:.2f}"
    meta = f"{color}  ·  {condition}"

    image = Image.new("RGB", LABEL_PIXELS, "white")
    draw = ImageDraw.Draw(image)
    left = 12
    right = LABEL_PIXELS[0] - 12

    id_font = fit_font(draw, item_id, 250, 39, 26, bold=True)
    price_font = fit_font(draw, price, 270, 39, 25, bold=True)
    name_font = fit_font(draw, name, right - left, 29, 19, bold=False)
    meta_font = fit_font(draw, meta, right - left, 24, 17, bold=False)

    draw.text((left, 4), item_id, fill="black", font=id_font)
    price_box = draw.textbbox((0, 0), price, font=price_font)
    price_width = price_box[2] - price_box[0]
    draw.text((right - price_width, 4), price, fill="black", font=price_font)

    draw.text((left, 57), name, fill="black", font=name_font)
    draw.text((left, 105), meta, fill="black", font=meta_font)
    draw.line((left, 154, right, 154), fill="black", width=2)
    return image


class LabelPrinter:
    def __init__(self, printer: str, model: str, dry_run: bool = False):
        self.printer = printer
        self.model = model
        self.dry_run = dry_run
        if not dry_run:
            backend_name = guess_backend(printer)
            self.backend_class = backend_factory(backend_name)["backend_class"]
        else:
            self.backend_class = None

    def print_label(self, image: Image.Image, copies: int) -> None:
        copies = max(1, min(int(copies), 250))
        if self.dry_run:
            image.save("brickstore-label-preview.png")
            LOG.info("Dry-run: saved brickstore-label-preview.png (%d copies requested)", copies)
            return

        for _ in range(copies):
            qlr = BrotherQLRaster(self.model)
            create_label(
                qlr,
                image,
                LABEL_ID,
                threshold=70,
                cut=True,
                rotate="auto",
            )
            backend = self.backend_class(self.printer)
            try:
                backend.write(qlr.data)
            finally:
                backend.dispose()


class Handler(BaseHTTPRequestHandler):
    server_version = "BrickStoreBrotherQL/1.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        LOG.info("%s - %s", self.address_string(), fmt % args)

    def _json(self, status: int, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        if self.path.rstrip("/") == "/health":
            printer = self.server.label_printer
            self._json(200, {
                "ok": True,
                "model": printer.model,
                "printer": printer.printer,
                "label": LABEL_ID,
            })
            return
        self._json(404, {"ok": False, "error": "Not found"})

    def do_POST(self) -> None:
        if self.path.rstrip("/") != "/print":
            self._json(404, {"ok": False, "error": "Not found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 65536:
                raise ValueError("Invalid request size")
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("JSON body must be an object")
            if not str(payload.get("item_id", "")).strip():
                raise ValueError("item_id is required")

            image = render_label(payload)
            copies = int(payload.get("copies", 1))
            self.server.label_printer.print_label(image, copies)
            self._json(200, {"ok": True, "copies": copies, "label": LABEL_ID})
        except Exception as exc:  # expose the actual print error to BrickStore
            LOG.exception("Printing failed")
            self._json(500, {"ok": False, "error": str(exc)})


class LabelServer(ThreadingHTTPServer):
    def __init__(self, address: tuple[str, int], label_printer: LabelPrinter):
        super().__init__(address, Handler)
        self.label_printer = label_printer


def main() -> int:
    parser = argparse.ArgumentParser(description="BrickStore Brother QL label print service")
    parser.add_argument("--printer", required=not ("BROTHER_QL_PRINTER" in os.environ),
                        default=os.environ.get("BROTHER_QL_PRINTER"),
                        help="Printer descriptor, e.g. tcp://192.168.1.50:9100")
    parser.add_argument("--model", default=os.environ.get("BROTHER_QL_MODEL", "QL-810W"))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8013)
    parser.add_argument("--dry-run", action="store_true", help="Render PNG only; do not print")
    parser.add_argument("--log-level", default="INFO")
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(message)s",
    )

    label_printer = LabelPrinter(args.printer or "dry-run", args.model, args.dry_run)
    server = LabelServer((args.host, args.port), label_printer)
    LOG.info("Brother QL service listening on http://%s:%d", args.host, args.port)
    LOG.info("Printer: %s | Model: %s | Label: %s", args.printer, args.model, LABEL_ID)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
