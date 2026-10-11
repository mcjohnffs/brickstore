#!/usr/bin/env python3
"""BrickStore DK-11204 print service using brother_ql.

This deliberately mirrors the user's proven working Brother QL code path:
    BrotherQLRaster -> brother_ql.conversion.convert -> brother_ql.backends.helpers.send

It bypasses the Windows printer driver completely.
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
from brother_ql.conversion import convert
from brother_ql.backends.helpers import send
from brother_ql.raster import BrotherQLRaster

LOG = logging.getLogger("brickstore-brother-ql")
LABEL_ID = "17x54"          # Brother DK-11204
LABEL_PIXELS = (566, 165)   # landscape; convert(... rotate='auto') rotates for the die-cut label
DEFAULT_MODEL = "QL-810W"
DEFAULT_BACKEND = "network"


def _font_candidates(bold: bool) -> list[str]:
    if os.name == "nt":
        win = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"
        names = ["arialbd.ttf", "segoeuib.ttf"] if bold else ["arial.ttf", "segoeui.ttf"]
        return [str(win / name) for name in names]
    return [
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold
        else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf" if bold
        else "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    ]


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
    name_font = fit_font(draw, name, right - left, 29, 19)
    meta_font = fit_font(draw, meta, right - left, 24, 17)

    draw.text((left, 4), item_id, fill="black", font=id_font)
    price_box = draw.textbbox((0, 0), price, font=price_font)
    price_width = price_box[2] - price_box[0]
    draw.text((right - price_width, 4), price, fill="black", font=price_font)
    draw.text((left, 57), name, fill="black", font=name_font)
    draw.text((left, 105), meta, fill="black", font=meta_font)
    draw.line((left, 154, right, 154), fill="black", width=2)
    return image


class LabelPrinter:
    def __init__(self, printer: str, model: str, backend: str, dry_run: bool = False):
        self.printer = printer
        self.model = model
        self.backend = backend
        self.dry_run = dry_run

    def print_labels(self, label_payloads: list[dict[str, Any]]) -> int:
        images: list[Image.Image] = []
        for payload in label_payloads:
            copies = max(1, min(int(payload.get("copies", 1)), 250))
            image = render_label(payload)
            images.extend([image.copy() for _ in range(copies)])

        if not images:
            raise ValueError("No labels to print")

        if self.dry_run:
            for index, image in enumerate(images, start=1):
                image.save(f"brickstore-label-preview-{index:03d}.png")
            LOG.info("Dry-run: saved %d label preview(s)", len(images))
            return len(images)

        qlr = BrotherQLRaster(self.model)
        qlr.exception_on_warning = True
        instructions = convert(
            qlr=qlr,
            images=images,
            label=LABEL_ID,
            rotate="auto",
            threshold=70.0,
            dither=False,
            compress=False,
            red=False,
            dpi_600=False,
            hq=True,
            cut=True,
        )
        send(
            instructions=instructions,
            printer_identifier=self.printer,
            backend_identifier=self.backend,
            blocking=True,
        )
        return len(images)


class Handler(BaseHTTPRequestHandler):
    server_version = "BrickStoreBrotherQL/1.1"

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
                "backend": printer.backend,
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
            if length <= 0 or length > 1024 * 1024:
                raise ValueError("Invalid request size")
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("JSON body must be an object")

            if isinstance(payload.get("labels"), list):
                labels = [entry for entry in payload["labels"] if isinstance(entry, dict)]
            else:
                labels = [payload]

            if not labels:
                raise ValueError("No labels supplied")
            for entry in labels:
                if not str(entry.get("item_id", "")).strip():
                    raise ValueError("item_id is required for every label")

            printed = self.server.label_printer.print_labels(labels)
            self._json(200, {"ok": True, "printed": printed, "label": LABEL_ID})
        except Exception as exc:
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
                        help="Printer identifier, e.g. tcp://192.168.1.7")
    parser.add_argument("--model", default=os.environ.get("BROTHER_QL_MODEL", DEFAULT_MODEL))
    parser.add_argument("--backend", default=os.environ.get("BROTHER_QL_BACKEND", DEFAULT_BACKEND))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8013)
    parser.add_argument("--dry-run", action="store_true", help="Render PNG only; do not print")
    parser.add_argument("--log-level", default="INFO")
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level.upper(), logging.INFO),
        format="%(asctime)s %(levelname)s %(message)s",
    )

    label_printer = LabelPrinter(args.printer or "dry-run", args.model, args.backend, args.dry_run)
    server = LabelServer((args.host, args.port), label_printer)
    LOG.info("Brother QL service listening on http://%s:%d", args.host, args.port)
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
