#!/usr/bin/env python3
"""Regenerate the drawing outputs from the KiCad files with kicad-cli (KiCad 7 to 10).

    pip install pypdf reportlab
    python3 tools/make_outputs.py

Writes
  outputs/sensor-board/Vantage_Sensor_Schematic.pdf
  outputs/sensor-board/Vantage_Sensor_Copper-Artwork_Letter.pdf   (1:1, four copies, portrait)
  outputs/sensor-board/Vantage_Sensor_Copper-Artwork_A4.pdf       (1:1, four copies, portrait)
  outputs/sensor-board/Vantage_Sensor_Assembly-Drawing.pdf        (2:1, component side + copper side)
  outputs/power-board/Vantage_Power_Schematic.pdf
  docs/src/figures/sensor_*.svg                                   (figures used by the documents)
The BOM and drill CSV files are kept by hand.
"""
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from pypdf import PdfReader, PdfWriter, Transformation
from reportlab.pdfgen import canvas

ROOT = Path(__file__).resolve().parent.parent
SB, PB = ROOT / "sensor-board", ROOT / "power-board"
OUT_S, OUT_P = ROOT / "outputs" / "sensor-board", ROOT / "outputs" / "power-board"
FIG = ROOT / "docs" / "src" / "figures"
CLI = os.environ.get("KICAD_CLI", "kicad-cli")
MMPT = 72 / 25.4
PAPERS = {"A4": (210.0, 297.0), "USLetter": (215.9, 279.4)}      # portrait width, height in mm


def run(*cmd):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"failed: {' '.join(map(str, cmd))}\n{r.stdout}{r.stderr}")


def board_box(pcb_text):
    """board outline (x0, y0, x1, y1) in page mm, from the Edge.Cuts items"""
    xs, ys = [], []
    starts = [m.start() for m in re.finditer(r"\((?:gr_line|gr_rect)\b", pcb_text)]
    for i, s in enumerate(starts):
        item = pcb_text[s:starts[i + 1] if i + 1 < len(starts) else s + 800]
        if '(layer "Edge.Cuts")' not in item:
            continue
        m = re.search(r"\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)", item)
        xs += [float(m.group(1)), float(m.group(3))]
        ys += [float(m.group(2)), float(m.group(4))]
    return min(xs), min(ys), max(xs), max(ys)


def board_copy(tmp, paper, portrait=True, hide_values=False):
    """copy of the board with another paper size, same file name (the title block shows it)"""
    text = (SB / "Vantage_Sensor.kicad_pcb").read_text(encoding="utf-8")
    text = re.sub(r'\(paper "[^"]*"( portrait)?\)', f'(paper "{paper}"{" portrait" if portrait else ""})', text, count=1)
    if hide_values:      # move value texts off the fabrication layers so only references show
        text = re.sub(r'(\((?:fp_text value|property "Value") "[^"]*"\s*(?:\(at [^)]*\)\s*)?(?:\(unlocked [^)]*\)\s*)?)\(layer "([FB])\.Fab"\)',
                      r'\1(layer "User.9")', text)
    d = Path(tmp) / f"{paper}{'p' if portrait else 'l'}{'v' if hide_values else ''}"
    d.mkdir(parents=True, exist_ok=True)
    p = d / "Vantage_Sensor.kicad_pcb"
    p.write_text(text, encoding="utf-8")
    return p, text


def pdf(pcb, out, layers, frame=False, mirror=False, drill="0"):
    cmd = [CLI, "pcb", "export", "pdf", "--layers", layers, "--black-and-white", "--drill-shape-opt", drill, "-o", out]
    if frame:
        cmd.append("--ibt")
    if mirror:
        cmd.append("--mirror")
    run(*cmd, pcb)
    return PdfReader(str(out)).pages[0]


def artwork(tmp, paper, out):
    pcb, text = board_copy(tmp, paper)
    x0, y0, x1, y1 = board_box(text)
    page = pdf(pcb, Path(tmp) / "aw_framed.pdf", "B.Cu,Edge.Cuts,Dwgs.User", frame=True, drill="1")
    bare = pdf(pcb, Path(tmp) / "aw_bare.pdf", "B.Cu,Edge.Cuts,Dwgs.User", drill="1")
    pitch = (y1 - y0) + 15.0                            # board + scale line + gap
    for k in (1, 2, 3):
        page.merge_transformed_page(bare, Transformation().translate(0, -k * pitch * MMPT))
    w = PdfWriter()
    w.add_page(page)
    w.add_metadata({"/Title": "Vantage - Sensor board - copper artwork (B.Cu) 1:1", "/Author": "Team sevenUP"})
    with open(out, "wb") as f:
        w.write(f)


def label(text, size_mm, y_mm):
    buf = io.BytesIO()
    c = canvas.Canvas(buf, pagesize=(size_mm[0] * MMPT, size_mm[1] * MMPT))
    c.setFont("Helvetica", 9)
    c.drawCentredString(size_mm[0] / 2 * MMPT, (size_mm[1] - y_mm) * MMPT, text)
    c.save()
    buf.seek(0)
    return PdfReader(buf).pages[0]


def assembly_drawing(tmp, out, scale=2.0):
    pcb, text = board_copy(tmp, "A4", portrait=False)
    x0, y0, x1, y1 = board_box(text)
    W, H = 297.0, 210.0
    frame = Path(tmp) / "frame.pdf"
    w = PdfWriter()
    views = (("F.Cu,F.Fab,F.SilkS,Edge.Cuts", False, "VIEW FROM COMPONENT SIDE  -  SCALE 2:1"),
             ("B.Mask,B.Fab,Edge.Cuts", True, "VIEW FROM COPPER SIDE (SMD PARTS AND PADS)  -  SCALE 2:1"))
    for i, (layers, mirror, text_label) in enumerate(views):
        page = pdf(pcb, frame, "User.9", frame=True)
        board = pdf(pcb, Path(tmp) / f"view{i}.pdf", layers, mirror=mirror)
        cx = (x0 + x1) / 2
        cx = W - cx if mirror else cx
        cy = (y0 + y1) / 2
        tx, ty = W / 2, 95.0                            # where the board centre goes (mm from top-left)
        t = (Transformation().translate(-cx * MMPT, -(H - cy) * MMPT).scale(scale, scale)
             .translate(tx * MMPT, (H - ty) * MMPT))
        page.merge_transformed_page(board, t)
        page.merge_page(label(text_label, (W, H), ty + (y1 - y0) * scale / 2 + 16))
        w.add_page(page)
    w.add_metadata({"/Title": "Vantage - Sensor board - assembly drawing", "/Author": "Team sevenUP"})
    with open(out, "wb") as f:
        w.write(f)


def figures(tmp):
    FIG.mkdir(parents=True, exist_ok=True)
    pcb, _ = board_copy(tmp, "A4", hide_values=True)
    views = {"sensor_component_side": ("F.Cu,F.Fab,F.SilkS,Edge.Cuts", []),
             "sensor_copper_side": ("B.Mask,B.Fab,Edge.Cuts", ["--mirror"]),
             "sensor_copper_artwork": ("B.Cu,Edge.Cuts", [])}
    for name, (layers, extra) in views.items():
        out = FIG / f"{name}.svg"
        run(CLI, "pcb", "export", "svg", "--layers", layers, *extra, "--black-and-white", "--page-size-mode", "2",
            "--exclude-drawing-sheet", "-o", out, pcb)
        svg = out.read_text(encoding="utf-8")       # drop the export time stamp so unchanged figures give no diff
        out.write_text(re.sub(r"<title>.*?</title>", f"<title>{name}</title>", svg, count=1), encoding="utf-8")


def main():
    if not shutil.which(CLI):
        sys.exit("kicad-cli not found: install KiCad or set KICAD_CLI to its path")
    OUT_S.mkdir(parents=True, exist_ok=True)
    OUT_P.mkdir(parents=True, exist_ok=True)
    run(CLI, "sch", "export", "pdf", "-o", OUT_S / "Vantage_Sensor_Schematic.pdf", SB / "Vantage_Sensor.kicad_sch")
    run(CLI, "sch", "export", "pdf", "-o", OUT_P / "Vantage_Power_Schematic.pdf", PB / "Vantage_Power.kicad_sch")
    with tempfile.TemporaryDirectory() as tmp:
        artwork(tmp, "USLetter", OUT_S / "Vantage_Sensor_Copper-Artwork_Letter.pdf")
        artwork(tmp, "A4", OUT_S / "Vantage_Sensor_Copper-Artwork_A4.pdf")
        assembly_drawing(tmp, OUT_S / "Vantage_Sensor_Assembly-Drawing.pdf")
        figures(tmp)
    print("outputs written")


if __name__ == "__main__":
    main()
