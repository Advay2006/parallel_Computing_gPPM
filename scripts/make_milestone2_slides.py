"""Build the presentation spanning the paper and project milestones.

Run with `python3 scripts/make_milestone2_slides.py`. Requires python-pptx.
Slide-by-slide speaking scripts live in Milestone2_Speaker_Notes.md and are
embedded in the PPTX's Notes pages for LibreOffice Impress.
"""

from pathlib import Path
import re

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Inches, Pt


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "gPPM_Milestone2_Clean.pptx"
SPEAKER_GUIDE = ROOT / "Milestone2_Speaker_Notes.md"
prs = Presentation()
prs.slide_width = Inches(13.333)
prs.slide_height = Inches(7.5)
BLANK = prs.slide_layouts[6]

NAVY = "1D3047"
BLUE = "315F8A"
TEAL = "3472B0"
ORANGE = "A86646"
LIGHT = "F4F6F8"
PALE_BLUE = "E6EEF5"
PALE_TEAL = "EBF2FA"
PALE_ORANGE = "F7EDE7"
WHITE = "FFFFFF"
SLATE = "56677B"
FAINT = "D1DAE1"


def col(hex_value):
    return RGBColor.from_string(hex_value)


def flat(shape):
    """Use flat vector shapes; PowerPoint's default shape style adds a shadow."""
    for effect in shape._element.xpath("p:style/a:effectRef"):
        effect.set("idx", "0")
    return shape


def rect(slide, x, y, w, h, fill=WHITE, stroke=None, radius=False, sw=1):
    shp = slide.shapes.add_shape(
        MSO_SHAPE.RECTANGLE,
        Inches(x), Inches(y), Inches(w), Inches(h),
    )
    shp.fill.solid()
    shp.fill.fore_color.rgb = col(fill)
    if stroke:
        shp.line.color.rgb = col(stroke)
        shp.line.width = Pt(sw)
    else:
        shp.line.fill.background()
    return flat(shp)


def label(slide, value, x, y, w, h, size=20, color=NAVY, bold=False,
          align="left", valign="middle", font="Liberation Sans", margin=0):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.clear()
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(margin)
    tf.margin_top = tf.margin_bottom = Inches(margin)
    tf.vertical_anchor = {
        "top": MSO_ANCHOR.TOP,
        "middle": MSO_ANCHOR.MIDDLE,
        "bottom": MSO_ANCHOR.BOTTOM,
    }[valign]
    p = tf.paragraphs[0]
    p.text = value
    p.alignment = {"left": PP_ALIGN.LEFT, "center": PP_ALIGN.CENTER,
                   "right": PP_ALIGN.RIGHT}[align]
    p.font.name = font
    p.font.size = Pt(size)
    p.font.bold = bold
    p.font.color.rgb = col(color)
    return box


def dot(slide, x, y, diameter, fill, stroke=None):
    s = slide.shapes.add_shape(
        MSO_SHAPE.OVAL, Inches(x), Inches(y), Inches(diameter), Inches(diameter)
    )
    s.fill.solid()
    s.fill.fore_color.rgb = col(fill)
    if stroke:
        s.line.color.rgb = col(stroke)
        s.line.width = Pt(1.4)
    else:
        s.line.fill.background()
    return flat(s)


def connector(slide, x1, y1, x2, y2, color=SLATE, width=2):
    from pptx.enum.shapes import MSO_CONNECTOR
    line = slide.shapes.add_connector(
        MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2)
    )
    line.line.color.rgb = col(color)
    line.line.width = Pt(width)
    return flat(line)


def arrow(slide, x, y, w=.48, h=.3, color=BLUE):
    shape = slide.shapes.add_shape(
        MSO_SHAPE.RIGHT_ARROW, Inches(x), Inches(y), Inches(w), Inches(h)
    )
    shape.fill.solid()
    shape.fill.fore_color.rgb = col(color)
    shape.line.fill.background()
    return flat(shape)


def base(kicker, title, num):
    slide = prs.slides.add_slide(BLANK)
    num = len(prs.slides)
    stage = ("THE PAPER" if num <= 4 else
             "MILESTONE 1" if num <= 7 else
             "MILESTONE 2" if num <= 16 else
             "MILESTONE 3" if num == 17 else
             "REFERENCES" if num == 18 else "Q&A BACKUP")
    slide.background.fill.solid()
    slide.background.fill.fore_color.rgb = col(WHITE)
    section = stage if stage == "REFERENCES" else f"{stage}  /  {kicker.upper()}"
    accent = ORANGE if num > 18 else BLUE
    label(slide, section, .68, .36, 10.95, .25, size=11,
          color=accent, bold=True)
    label(slide, title, .68, .76, 12.0, .7, size=28, bold=True)
    rect(slide, .68, 1.51, 11.98, .018, FAINT)
    rect(slide, .68, 1.51, .73, .036, accent)
    label(slide, "Q&A appendix  /  not in the main talk" if num > 18 else
          "gPPM  /  Parallel Computing", .68, 7.16, 6.6, .19,
          size=9, color=SLATE)
    label(slide, f"{num:02d}", 12.1, 7.14, .55, .23,
          size=10, color=SLATE, align="right")
    return slide


def sub(slide, sentence):
    label(slide, sentence, .68, 1.65, 11.89, .37, size=16, color=SLATE)


def stripe(slide, x, y, cell=.72, gap=.075, fail=None, highlight_rows=None,
           show_numbers=True):
    fail = set(fail or ())
    highlight_rows = set(highlight_rows or ())
    for d in range(4):
        label(slide, f"D{d}", x + d*(cell+gap), y-.33, cell, .25,
              12, SLATE, align="center")
    for row in range(4):
        label(slide, f"R{row}", x-.51, y+row*(cell+gap), .43, cell,
              12, SLATE, align="right")
        for disk in range(4):
            c = row*4+disk
            fill = ORANGE if c in fail else (
                PALE_TEAL if row in highlight_rows else PALE_BLUE
            )
            rect(slide, x+disk*(cell+gap), y+row*(cell+gap), cell, cell,
                 fill, radius=True)
            if show_numbers:
                label(slide, f"{c}", x+disk*(cell+gap),
                      y+row*(cell+gap), cell, cell, 17,
                      WHITE if c in fail else NAVY, bold=True, align="center")


# 1 — Cover
slide = prs.slides.add_slide(BLANK)
slide.background.fill.solid()
slide.background.fill.fore_color.rgb = col(NAVY)
rect(slide, .83, .78, 1.13, .065, TEAL)
label(slide, "PARALLEL COMPUTING  /  RESEARCH REPRODUCTION", .85, 1.03, 11, .32,
      14, PALE_BLUE, bold=True)
label(slide, "Partitioned Matrix Recovery", .84, 1.72, 11.7, .85,
      45, WHITE, bold=True)
label(slide, "for SD Erasure Codes", .84, 2.58, 11.7, .85,
      45, WHITE, bold=True)
label(slide, "Paper  →  M1 baseline  →  M2 PPM + OpenMP  →  M3 gPPM", .85,
      3.7, 11.54, .5, 21, PALE_BLUE)
for i, (large, small) in enumerate([("35 → 29", "FIG. 3 REGION OPERATIONS"),
                                    ("855 MiB/s", "M1 DECODE · m=1, s=1"),
                                    ("2.33×", "M2 BEST MEDIAN SPEEDUP")]):
    x = .85 + i*4.05
    rect(slide, x, 5.07, 3.52, 1.48, "213C60", radius=True)
    label(slide, large, x+.22, 5.25, 3.1, .72,
          29 if i == 1 else 35, WHITE, bold=True)
    label(slide, small.upper(), x+.22, 6.04, 3.12, .28,
          12, "B5D0E6", bold=True)
label(slide, "Based on Li et al., ACM TACO 20(4), 2023", .85, 7.13,
      9.0, .22, 10, "B5D0E6")


# 2 — Failure story
slide = base("motivation", "A disk failure plus one more sector", 2)
sub(slide, "SD codes recover both complete disk failures and additional sector erasures.")
stripe(slide, 1.35, 2.56, cell=.83, gap=.10,
       fail=[2, 6, 10, 13, 14])
label(slide, "disk 2", 3.04, 6.3, 1.25, .3, 15, BLUE,
      bold=True, align="center")
rect(slide, 8.42, 2.46, 3.63, 1.18, PALE_BLUE, radius=True)
label(slide, "DISK", 8.75, 2.64, 2.86, .7, 29, BLUE, bold=True,
      align="center")
label(slide, "+", 9.85, 3.9, .73, .48, 31, SLATE, align="center")
rect(slide, 8.42, 4.62, 3.63, 1.18, PALE_ORANGE, radius=True)
label(slide, "SECTOR", 8.75, 4.8, 2.86, .7, 29, ORANGE, bold=True,
      align="center")


# 3 — Parameters and stripe
slide = base("model", "One stripe: n disks × r rows", 3)
sub(slide, "The encoded unit is a grid of sectors across disks, not a single disk block.")
stripe(slide, 1.35, 2.55, cell=.79, gap=.085,
       fail=[2, 6, 10, 13, 14])
label(slide, "n = 4", 1.55, 6.15, 3.06, .45, 23, NAVY,
      bold=True, align="center")
data = [("m", "1", "disk failure"), ("s", "1", "extra sector"),
        ("r", "4", "rows per disk"), ("w", "8", "GF word bits")]
for idx, (letter, value, word) in enumerate(data):
    x = 6.38 + (idx % 2) * 3.03
    y = 2.17 + (idx // 2) * 2.03
    rect(slide, x, y, 2.64, 1.62, LIGHT, radius=True)
    label(slide, f"{letter} = {value}", x+.2, y+.15, 2.26, .65,
          29, BLUE if idx < 2 else NAVY, bold=True)
    label(slide, word, x+.2, y+.97, 2.23, .33, 16, SLATE)
label(slide, "SD(4,4; 1,1)  ·  GF(2⁸)", 7.0, 6.25, 5.1, .35,
      19, SLATE, align="center")


# 4 — H mapped to sectors
slide = base("model", "H: each column is a sector", 4)
sub(slide, "Rows represent parity equations: four local checks and one dense check.")
faults = {2, 6, 10, 13, 14}
x0, y0, cw, ch, gap = 2.25, 2.35, .53, .63, .045
for group, group_label in enumerate(["0–3", "4–7", "8–11", "12–15"]):
    label(slide, group_label, x0+group*4*(cw+gap), 2.07,
          4*cw+3*gap, .24, 12, SLATE, align="center")
for eq in range(5):
    label(slide, f"R{eq}" if eq < 4 else "DENSE", 1.15,
          y0+eq*(ch+gap), 1.03, ch, 11 if eq == 4 else 15,
          SLATE, bold=True, align="right")
    for c in range(16):
        on = (eq == c//4) if eq < 4 else True
        fill = (ORANGE if c in faults else (TEAL if eq == 4 else BLUE)) if on else LIGHT
        rect(slide, x0+c*(cw+gap), y0+eq*(ch+gap), cw, ch, fill)
        if on and c in faults:
            label(slide, str(c), x0+c*(cw+gap), y0+eq*(ch+gap),
                  cw, ch, 13, WHITE, bold=True, align="center")
label(slide, "local", 11.85, 2.93, 1.0, .28, 16, BLUE, bold=True)
label(slide, "global", 11.78, 5.18, 1.0, .28, 16, TEAL, bold=True)
label(slide, "H · B = 0", 3.8, 6.08, 5.94, .48, 28,
      NAVY, bold=True, align="center")
for x, color, meaning in [(2.27, BLUE, "coefficient"),
                           (5.18, LIGHT, "zero"),
                           (6.82, ORANGE, "failed column")]:
    rect(slide, x, 6.64, .19, .19, color,
         stroke=FAINT if color == LIGHT else None)
    label(slide, meaning, x+.29, 6.61, 2.39, .27,
          11, SLATE)


# 5 — Recovery equation
slide = base("recovery", "Separate missing from surviving", 5)
sub(slide, "Reorder the columns of H by failure status; solve for the unknown sectors.")
rect(slide, .91, 2.13, 4.0, 2.95, PALE_ORANGE, radius=True)
label(slide, "F · B(F)", 1.41, 2.39, 3.02, 1.12, 37, ORANGE,
      bold=True, align="center")
label(slide, "FAILED / UNKNOWN", 1.42, 3.84, 3.0, .39, 15, ORANGE,
      bold=True, align="center")
label(slide, "+", 5.21, 3.11, .8, .65, 40, SLATE, align="center")
rect(slide, 6.25, 2.13, 4.0, 2.95, PALE_BLUE, radius=True)
label(slide, "S · B(S)", 6.75, 2.39, 3.02, 1.12, 37, BLUE,
      bold=True, align="center")
label(slide, "SURVIVING / KNOWN", 6.72, 3.84, 3.1, .39, 15,
      BLUE, bold=True, align="center")
label(slide, "= 0", 10.56, 3.09, 1.58, .65, 40, NAVY, align="center")
label(slide, "F · B₍F₎ + S · B₍S₎ = 0", 1.15, 5.69, 10.85, .62,
      30, NAVY, bold=True, align="center")
label(slide, "B₍F₎ = F⁻¹ · S · B₍S₎", 2.26, 6.38, 8.74, .46,
      22, BLUE, align="center")


# 6 — Milestone 1 validated baseline
slide = base("validated baseline", "Milestone 1: sequential recovery", 6)
sub(slide, "The baseline remains the reference implementation for every comparison.")
rect(slide, 1.08, 2.28, 6.73, 3.71, LIGHT)
label(slide, "NORMAL SEQUENCE", 1.4, 2.62, 5.7, .38, 17,
      BLUE, bold=True)
label(slide, "T = S · B₍S₎", 1.4, 3.38, 5.8, .48,
      29, NAVY)
label(slide, "B₍F₎ = F⁻¹ · T", 1.4, 4.2, 5.8, .48,
      29, NAVY)
label(slide, "C = u(S) + u(F⁻¹)", 1.4, 5.14, 5.8, .45,
      21, SLATE)
rect(slide, 8.24, 2.28, 3.91, 1.7, PALE_BLUE)
label(slide, "35", 8.5, 2.49, 3.39, .91, 44,
      BLUE, bold=True, align="center")
label(slide, "Figure 2 region calls", 8.5, 3.46, 3.39, .3,
      14, SLATE, align="center")
rect(slide, 8.24, 4.25, 3.91, 1.74, PALE_TEAL)
label(slide, "GF(2ʷ)", 8.5, 4.43, 3.39, .87,
      34, TEAL, bold=True, align="center")
label(slide, "w = 8, 16, 32", 8.5, 5.37, 3.39, .34,
      14, SLATE, align="center")
label(slide, "ec_recover() · scalar GF arithmetic · single-threaded", 1.08,
      6.33, 11.31, .39, 17, NAVY)


# 7 — Milestone 1 performance rather than another correctness assertion.
slide = base("measured results", "Milestone 1: sequential throughput", 7)
sub(slide, "32 MiB stripe · mean useful-data throughput (MiB/s) · s = 1, GF(2⁸)")
chart_left, chart_right, chart_top, chart_bottom = 1.47, 9.73, 2.39, 5.99
for value in [0, 250, 500, 750, 1000]:
    y = chart_bottom - value / 1000 * (chart_bottom-chart_top)
    connector(slide, chart_left, y, chart_right, y, FAINT, .8)
    label(slide, str(value), .76, y-.14, .57, .27, 12,
          SLATE, align="right")
for i, (enc, dec) in enumerate([(852.60, 854.58),
                                 (444.98, 447.81),
                                 (282.56, 275.55)]):
    x = 2.06+i*2.63
    for j, (value, color) in enumerate([(enc, "85A9CD"), (dec, NAVY)]):
        bar_x = x+j*.69
        height = value / 1000 * (chart_bottom-chart_top)
        rect(slide, bar_x, chart_bottom-height, .58, height, color)
        label(slide, str(round(value)), bar_x-.06,
              chart_bottom-height-.38, .71, .29, 12,
              NAVY, bold=True, align="center")
    label(slide, f"m={i+1}", x+.02, 6.1, 1.26, .34,
          17, NAVY, align="center")
rect(slide, 10.34, 2.52, .19, .19, "85A9CD")
label(slide, "encode", 10.65, 2.48, 1.36, .29, 15, NAVY)
rect(slide, 10.34, 2.96, .19, .19, NAVY)
label(slide, "decode", 10.65, 2.93, 1.36, .29, 15, NAVY)
label(slide, "9", 10.23, 4.13, 2.25, .78, 41, BLUE,
      bold=True, align="center")
label(slide, "SD settings", 10.23, 4.93, 2.25, .36,
      15, SLATE, align="center")
label(slide, "Three of nine settings shown · mean over 10 trials each", 1.47,
      6.59, 10.41, .3, 14, SLATE)


# 6 — Associativity
slide = base("insight 1", "Same answer. Different work.", 6)
sub(slide, "Associativity lets us combine small coefficient matrices before touching sector data.")
rect(slide, .92, 2.09, 5.43, 3.52, LIGHT, radius=True)
rect(slide, 7.0, 2.09, 5.43, 3.52, PALE_TEAL, radius=True)
label(slide, "NORMAL", 1.25, 2.43, 4.8, .33, 15, BLUE,
      bold=True, align="center")
label(slide, "S · B₍S₎ → T", 1.26, 3.17, 4.78, .48, 25,
      NAVY, align="center")
label(slide, "F⁻¹ · T → B₍F₎", 1.26, 3.95, 4.78, .48, 25,
      NAVY, align="center")
label(slide, "u(S) + u(F⁻¹)", 1.26, 5.01, 4.78, .38, 18,
      SLATE, align="center")
label(slide, "u(M): nonzero coefficients / region calls", 1.26, 5.48,
      4.78, .28, 13, SLATE, align="center")
label(slide, "MATRIX-FIRST", 7.36, 2.43, 4.72, .33, 15,
      TEAL, bold=True, align="center")
label(slide, "F⁻¹ · S → G", 7.35, 3.17, 4.75, .48, 25,
      NAVY, align="center")
label(slide, "G · B₍S₎ → B₍F₎", 7.35, 3.95, 4.75, .48, 25,
      NAVY, align="center")
label(slide, "u(G)", 7.35, 5.01, 4.75, .38, 18,
      SLATE, align="center")
label(slide, "Tiny matrix math can eliminate large-region operations.",
      1.08, 6.25, 11.2, .43, 18, NAVY, align="center")


# 7 — partition from real Figure 3
slide = base("insight 2", "Partition by stripe row", 7)
sub(slide, "Rows with exactly m failed sectors can be solved using only local parity.")
stripe(slide, 1.11, 2.57, cell=.77, gap=.12,
       fail=[2, 6, 10, 13, 14], highlight_rows=[0, 1, 2])
for i, name in enumerate(["H₀", "H₁", "H₂"]):
    y = 2.25 + i*.98
    rect(slide, 6.28, y, 2.13, .73, PALE_TEAL, radius=True)
    label(slide, name, 6.28, y, 2.13, .73, 24, TEAL,
          bold=True, align="center")
rect(slide, 6.28, 5.19, 2.13, .73, PALE_ORANGE, radius=True)
label(slide, "Hᵣₑₛₜ", 6.28, 5.19, 2.13, .73, 21, ORANGE,
      bold=True, align="center")
label(slide, "{2}", 9.0, 2.37, 1.1, .48, 21, NAVY)
label(slide, "{6}", 9.0, 3.35, 1.1, .48, 21, NAVY)
label(slide, "{10}", 9.0, 4.33, 1.1, .48, 21, NAVY)
label(slide, "{13, 14}", 9.0, 5.31, 1.9, .48, 21, NAVY)
rect(slide, 10.79, 2.54, 1.54, 2.54, PALE_TEAL, radius=True)
label(slide, "3", 10.94, 2.78, 1.24, 1.06, 49, TEAL,
      bold=True, align="center")
label(slide, "groups", 10.94, 4.08, 1.24, .4, 15, TEAL,
      align="center")
label(slide, "row 3: 2 failures, 1 local equation", 6.28, 6.25,
      6.15, .37, 16, SLATE)


# 8 — PPM flow
slide = base("algorithm", "Parallel first. Dependent last.", 8)
sub(slide, "Validate and prepare before writing; solve independent rows in parallel.")
for i, (heading, detail) in enumerate([
    ("1  Validate", "H, faults, sector size"),
    ("2  Partition", "row-local vs remainder"),
    ("3  Prepare", "split and invert jobs"),
]):
    x = .78 + i*2.36
    rect(slide, x, 3.16, 2.13, 1.36, LIGHT)
    label(slide, heading, x+.18, 3.4, 1.81, .4,
          18, NAVY, bold=True)
    label(slide, detail, x+.18, 3.98, 1.81, .32,
          12, SLATE)
    if i < 2:
        arrow(slide, x+2.18, 3.68, .14, .25, BLUE)
for i, title in enumerate(["H₀", "H₁", "H₂ …"]):
    y = 2.29+i*1.02
    rect(slide, 8.13, y, 1.67, .82, WHITE, stroke=BLUE, sw=1.6)
    label(slide, title, 8.22, y+.11, 1.49, .35,
          22, BLUE, bold=True, align="center")
    label(slide, "matrix-first", 8.19, y+.54, 1.55, .22,
          11, SLATE, align="center")
    connector(slide, 7.89, 3.85, 8.10, y+.41, color=SLATE, width=1)
    arrow(slide, 9.82, y+.27, .23, .25, SLATE)
rect(slide, 10.1, 2.24, .13, 3.06, ORANGE)
label(slide, "barrier", 9.7, 5.41, .98, .32,
      14, ORANGE, bold=True, align="center")
arrow(slide, 10.3, 3.35, .29, .27, NAVY)
rect(slide, 10.69, 2.98, 1.86, 1.02, NAVY)
label(slide, "Hᵣₑₛₜ", 10.77, 3.1, 1.7, .43, 22,
      WHITE, bold=True, align="center")
label(slide, "normal", 10.77, 3.58, 1.7, .28, 13,
      WHITE, align="center")
connector(slide, 11.61, 4.01, 11.61, 4.34, NAVY, 1.5)
rect(slide, 10.69, 4.44, 1.86, .84, LIGHT)
label(slide, "Aggregate counts", 10.77, 4.56, 1.7, .32,
      14, NAVY, bold=True, align="center")
label(slide, "TLS + remainder", 10.77, 4.91, 1.7, .21,
      11, SLATE, align="center")
rect(slide, .78, 6.05, 11.77, .56, LIGHT)
label(slide, "ppm_recover_sd()    ·    #pragma omp for    ·    workers = min(T, p)",
      1.06, 6.19, 11.15, .31, 15, SLATE)


# Race safety in the same 2×2 analytical format as the reference design.
slide = base("implementation", "Parallel without races", 9)
sub(slide, "Four constraints make the parallel implementation predictable and safe.")
rules = [
    ("Disjoint writes", "Each job writes only its own missing sectors and reads surviving data."),
    ("Mandatory barrier", "The remainder waits before reading independently recovered sectors."),
    ("Thread-local counts", "A private counter avoids races and hot-path atomic contention."),
    ("Prepare before write", "All splits and inversions finish before any worker modifies sectors."),
]
for i, (heading, detail) in enumerate(rules):
    x = .91 + (i % 2)*5.89
    y = 2.24 + (i // 2)*2.03
    rect(slide, x, y, 5.6, 1.72, LIGHT)
    rect(slide, x, y, .06, 1.72, ORANGE if i == 1 else BLUE)
    label(slide, str(i+1), x+.26, y+.22, .51, .37,
          18, ORANGE if i == 1 else BLUE, bold=True)
    label(slide, heading, x+.85, y+.22, 4.38, .39,
          20, ORANGE if i == 1 else NAVY, bold=True)
    label(slide, detail, x+.85, y+.76, 4.28, .75,
          16, NAVY, valign="top")
rect(slide, .91, 6.40, 11.49, .36, LIGHT)
label(slide, "Threads are capped at p; matrices are read-only and worker status is checked after the loop.",
      1.05, 6.41, 11.18, .32, 14, SLATE)


# 10 — Figure 3 exact cost
slide = base("reproduced result", "Figure 3: 35 → 29 operations", 10)
sub(slide, "Whole-sector multiply-XOR operations in the paper's example.")
label(slide, "BASELINE", 1.02, 2.23, 3.0, .35,
      16, NAVY, bold=True)
rect(slide, 1.04, 2.91, 5.24, 1.04, PALE_BLUE)
rect(slide, 6.28, 2.91, 3.09, 1.04, BLUE)
label(slide, "u(S)", 1.5, 2.98, 3.8, .28, 13, BLUE,
      align="center")
label(slide, "22", 1.5, 3.36, 3.8, .46, 27, BLUE, bold=True,
      align="center")
label(slide, "u(F⁻¹)", 6.28, 2.98, 3.09, .28, 13, WHITE,
      align="center")
label(slide, "13", 6.28, 3.36, 3.09, .46, 27, WHITE, bold=True,
      align="center")
label(slide, "35", 9.67, 2.97, 1.68, .88,
      45, NAVY, bold=True, align="right")
label(slide, "PPM", 1.02, 4.48, 3.0, .35,
      16, TEAL, bold=True)
for i in range(3):
    rect(slide, 1.04+i*.77, 5.06, .71, 1.04, TEAL)
    label(slide, "3", 1.04+i*.77, 5.32, .71, .53,
          25, WHITE, bold=True, align="center")
rect(slide, 3.35, 5.06, 4.76, 1.04, NAVY)
label(slide, "remainder", 3.35, 5.14, 4.76, .25, 13, WHITE,
      align="center")
label(slide, "20", 3.35, 5.46, 4.76, .46, 27, WHITE,
      bold=True, align="center")
label(slide, "29", 9.67, 5.12, 1.68, .88,
      45, BLUE, bold=True, align="right")
label(slide, "−17.1%", 11.18, 3.7, 1.37, .75,
      23, ORANGE, bold=True, align="center")
label(slide, "3 independent groups + dependent remainder · region calls", 1.04, 6.43, 8.17, .3,
      14, SLATE)


# Source and evaluation provenance
slide = base("evaluation", "Published coefficients. Synthetic bytes.", 11)
sub(slide, "Coefficients define the code; locally generated sectors test its recovery.")
for i, (heading, number, detail, clr) in enumerate([
    ("Coefficients", "3,969", "Plank's published FAST SD configurations", BLUE),
    ("Failure layouts", "7,833", "Feasible (configuration, z) cases", TEAL),
    ("Performance", "32 MiB", "Synthetic codeword, nine SD settings", NAVY),
]):
    x = .96 + i*4.16
    rect(slide, x, 2.39, 3.64, 3.32, LIGHT)
    rect(slide, x, 2.39, 3.64, .055, clr)
    label(slide, heading, x+.26, 2.73, 3.14, .34,
          16, SLATE, bold=True)
    label(slide, number, x+.24, 3.36, 3.18, .79,
          32, clr, bold=True)
    label(slide, detail, x+.26, 4.52, 3.09, .84,
          16, NAVY, valign="top")
label(slide, "Inputs: deterministic pseudo-random data and repeatable failure positions.",
      .96, 6.18, 11.9, .4, 16, SLATE)


# 13 — grid work reduction
slide = base("algorithmic benefit", "Fewer region operations in every tested layout", 13)
sub(slide, "Median reduction increases with the number of disk failures tolerated.")
values = [4.7, 13.3, 22.1]
for i, value in enumerate(values):
    x = 1.3 + i*3.14
    rect(slide, x, 2.27, 2.28, 3.32, LIGHT, radius=True)
    bar_h = 2.29 * value / 24
    rect(slide, x+.51, 5.1-bar_h, 1.24, bar_h,
         ["8AB3D9", BLUE, NAVY][i])
    label(slide, f"{value:.1f}%", x, 5.72, 2.28, .51,
          25, NAVY, bold=True, align="center")
    label(slide, f"m = {i+1}", x, 6.29, 2.28, .34,
          16, SLATE, align="center")
rect(slide, 10.7, 2.32, 1.9, 3.11, PALE_TEAL)
label(slide, "12.9%", 10.84, 3.06, 1.62, .78,
      25, TEAL, bold=True, align="center")
label(slide, "grid median", 10.83, 4.02, 1.62, .36,
      14, TEAL, align="center")


# 14 — s=1 scaling chart, only three series (see detailed notes for all nine)
slide = base("performance", "Scaling: one thread to eight", 14)
sub(slide, "n = r = 16, z = 1  ·  32 MiB  ·  10 trials / setting  ·  ratio of median times")
left, right, top, bottom = 1.54, 9.4, 2.13, 5.95
for val in [1.0, 1.5, 2.0, 2.5]:
    yy = bottom-(val-1.0)/(2.5-1.0)*(bottom-top)
    connector(slide, left, yy, right, yy, FAINT, .8)
    label(slide, f"{val:.1f}×", .58, yy-.17, .82, .34,
          13, SLATE, align="right")
series = [("m=1", [1.13, 1.31, 1.34, 1.41], "82AEDA"),
          ("m=2", [1.02, 1.25, 1.65, 1.77], BLUE),
          ("m=3", [1.19, 1.66, 2.10, 2.33], NAVY)]
xs = [1.85, 4.22, 6.6, 8.98]
for name, vals, clr in series:
    points = [(xx, bottom-(v-1.0)/(2.5-1.0)*(bottom-top))
              for xx, v in zip(xs, vals)]
    for (x1, y1), (x2, y2) in zip(points, points[1:]):
        connector(slide, x1, y1, x2, y2, clr, 3)
    for xx, yy in points:
        dot(slide, xx-.07, yy-.07, .14, clr)
for xx, t in zip(xs, [1, 2, 4, 8]):
    label(slide, str(t), xx-.29, 6.16, .58, .33,
          15, SLATE, align="center")
label(slide, "requested threads", 3.84, 6.54, 3.27, .33,
      15, SLATE, align="center")
for i, (name, _vals, clr) in enumerate(series):
    dot(slide, 10.22, 2.25+i*.54, .19, clr)
    label(slide, name, 10.55, 2.19+i*.54, 1.55, .33,
          16, NAVY)
label(slide, "s = 1", 10.15, 4.14, 2.28, .38,
      17, SLATE, align="center")
label(slide, "2.33×", 9.95, 5.07, 2.73, .75,
      37, BLUE, bold=True, align="center")
label(slide, "best median", 10.12, 5.89, 2.4, .39,
      14, SLATE, align="center")
label(slide, "m=3, s=1, T=8", 10.1, 6.36, 2.46, .31,
      14, SLATE, align="center")


# Limit to scaling: the bar is a schematic, not a proportionate measurement.
slide = base("discussion", "The remainder limits scaling", 15)
sub(slide, "Independent work can overlap; the dense-equation remainder waits for all workers.")
label(slide, "independent groups (parallel)", 1.13, 2.43, 6.17, .39,
      19, NAVY, bold=True)
label(slide, "schematic · not to scale", 8.84, 2.35, 3.2, .35,
      14, SLATE, align="right")
for i, name in enumerate(["H₀", "H₁", "H₂ …"]):
    y = 3.01+i*.45
    rect(slide, 1.13, y, 6.13, .35, TEAL)
    label(slide, name, 1.13, y+.015, 6.13, .32,
          15, WHITE, bold=True, align="center")
rect(slide, 7.34, 2.8, .13, 1.86, ORANGE)
label(slide, "barrier", 6.92, 4.76, 1.04, .34,
      15, ORANGE, bold=True, align="center")
rect(slide, 7.55, 3.1, 3.66, 1.21, NAVY)
label(slide, "Hᵣₑₛₜ (sequential)", 7.78, 3.47, 3.2, .46,
      19, WHITE, bold=True, align="center")
for i, word in enumerate(["Thread overhead", "Memory bandwidth", "Sequential tail"]):
    x = 1.18 + i*4.07
    rect(slide, x, 5.5, 3.18, .89, LIGHT)
    label(slide, word, x+.13, 5.71, 2.93, .47,
          17, NAVY, bold=True, align="center")


# Milestone 3 has its own destination slide after the M2 discussion.
slide = base("roadmap", "From PPM to generalized gPPM", 17)
sub(slide, "Milestone 2 establishes a verified SD-specific parallel baseline for the next stage.")
rect(slide, 1.03, 2.32, 4.65, 3.42, PALE_BLUE)
rect(slide, 7.72, 2.32, 4.62, 3.42, PALE_TEAL)
label(slide, "M2  /  PPM", 1.38, 2.66, 3.92, .49,
      22, BLUE, bold=True)
label(slide, "SD partitioning", 1.38, 3.54, 3.92, .42, 21, NAVY)
label(slide, "fixed matrix-first", 1.38, 4.18, 3.92, .42, 21, NAVY)
label(slide, "OpenMP recovery", 1.38, 4.82, 3.92, .42, 21, NAVY)
arrow(slide, 6.15, 3.72, .82, .52, BLUE)
label(slide, "M3  /  gPPM", 8.09, 2.66, 3.92, .49,
      22, TEAL, bold=True)
label(slide, "dynamic sequence choice", 8.09, 3.54, 3.92, .42, 20, NAVY)
label(slide, "cost: C = u + c·v", 8.09, 4.18, 3.92, .42, 20, NAVY)
label(slide, "u: nonzeros  ·  v: non-ones  ·  c: GF cost", 8.09, 4.62,
      3.92, .25, 11, SLATE)
label(slide, "RS comparison + SIMD", 8.09, 5.08, 3.92, .42, 20, NAVY)
label(slide, "M2 results  ·  35 → 29 operations  ·  12.9% median work reduction  ·  2.33× best speedup", 
      1.08, 6.29, 11.66, .45, 17, SLATE, align="center")


# 16 — References; concise enough for a projector, details in notes
slide = base("references", "Sources", 16)
sub(slide, "Algorithm, SD-code construction, and published coding parameters.")
refs = [
    ("01", "Li et al. · gPPM", "ACM TACO 20(4), 2023"),
    ("02", "Plank et al. · SD Codes", "FAST, 2013"),
    ("03", "Plank · SD encoder/decoder", "Technical report + FAST coefficients, 2013"),
]
for i, (num, title, detail) in enumerate(refs):
    y = 2.1+i*1.39
    rect(slide, 1.16, y, 10.94, 1.12, LIGHT, radius=True)
    label(slide, num, 1.39, y+.24, .67, .62, 27, BLUE, bold=True)
    label(slide, title, 2.2, y+.1, 6.4, .52, 23, NAVY, bold=True)
    label(slide, detail, 2.2, y+.65, 9.4, .32, 15, SLATE)
label(slide, "github.com/Advay2006/parallel_Computing_gPPM", 2.19,
      6.41, 8.82, .35, 16, BLUE, align="center")


# 19 — Clear boundary: these slides are not part of the normal talk.
slide = prs.slides.add_slide(BLANK)
slide.background.fill.solid()
slide.background.fill.fore_color.rgb = col(NAVY)
rect(slide, .87, 1.11, .9, .06, ORANGE)
label(slide, "BACKUP  /  OPEN ONLY FOR Q&A", .87, 1.49, 10.75, .4,
      16, "B7CEE0", bold=True)
label(slide, "Codebase & implementation details", .85, 2.45, 11.4, .87,
      39, WHITE, bold=True)
label(slide, "Repository map   ·   file responsibilities   ·   M1 → M2 code",
      .87, 3.57, 11.1, .56, 21, "B7CEE0")
label(slide, "The main presentation ends on the preceding references slide.",
      .87, 6.5, 11.0, .35, 15, "B7CEE0")
label(slide, "B1", 12.08, 7.11, .55, .23,
      11, "B7CEE0", align="right")


# 20 — Program structure at a glance.
slide = base("architecture", "What is in this codebase?", 20)
sub(slide, "The coefficient table builds H; both recovery paths share GF and matrix utilities.")
rect(slide, .98, 2.32, 3.12, 1.05, LIGHT)
label(slide, "FAST coefficients", 1.22, 2.48, 2.66, .36,
      19, NAVY, bold=True, align="center")
label(slide, "data/FAST-Coefficients.txt", 1.1, 2.97, 2.86, .26,
      12, SLATE, align="center")
arrow(slide, 4.19, 2.7, .38, .31, BLUE)
rect(slide, 4.71, 2.32, 3.21, 1.05, PALE_BLUE)
label(slide, "coefficients.c → sd_code.c", 4.87, 2.51, 2.91, .37,
      17, BLUE, bold=True, align="center")
label(slide, "construct parity-check matrix H", 4.81, 2.97, 3.01, .26,
      12, SLATE, align="center")
arrow(slide, 8.07, 2.7, .39, .31, BLUE)
rect(slide, 8.65, 2.12, 3.74, .99, PALE_BLUE)
label(slide, "codec.c   /   M1 baseline", 8.85, 2.34, 3.32, .52,
      20, NAVY, bold=True, align="center")
rect(slide, 8.65, 3.43, 3.74, .99, PALE_ORANGE)
label(slide, "ppm.c   /   M2 PPM", 8.85, 3.65, 3.32, .52,
      20, ORANGE, bold=True, align="center")
connector(slide, 8.27, 2.87, 8.27, 3.92, BLUE, 1.3)
arrow(slide, 8.27, 3.78, .29, .27, ORANGE)
label(slide, "shared foundation", .98, 4.77, 3.71, .32,
      15, SLATE, bold=True)
rect(slide, .98, 5.16, 11.41, .59, NAVY)
label(slide, "gf.c  ·  finite-field math     +     matrix.c  ·  inversion / multiplication",
      1.26, 5.28, 10.86, .34, 18, WHITE, align="center")
rect(slide, .98, 6.0, 11.41, .63, LIGHT)
label(slide, "Entry points: main.c / sweep.c / benchmark.c / ppm_sweep.c / ppm_benchmark.c",
      1.25, 6.15, 10.91, .34, 16, SLATE, align="center")


# 21 — Library responsibilities, more complete than the main presentation.
slide = base("file reference", "Core files: what each module owns", 21)
sub(slide, "Each header declares the API; the matching .c file contains its implementation.")
rect(slide, .87, 2.21, 11.57, .45, NAVY)
label(slide, "Module", 1.09, 2.28, 2.58, .28, 15, WHITE, bold=True)
label(slide, "Responsibility", 3.92, 2.28, 8.16, .28, 15, WHITE, bold=True)
for i, (name, description) in enumerate([
    ("gf.[ch]", "GF(2⁸/2¹⁶/2³²), mult_XORs, thread-local call counter"),
    ("matrix.[ch]", "GF matrices, inversion, mat_mul, nonzero counts"),
    ("coefficients.[ch]", "Read and validate the published FAST SD coefficient grid"),
    ("sd_code.[ch]", "Construct H; select coding sectors and failure layouts"),
    ("codec.[ch]", "Split F/S; normal and matrix-first decode; ec_recover baseline"),
    ("ppm.[ch]", "Partition SD jobs, OpenMP loop, barrier, remainder, stats"),
]):
    y = 2.67+i*.64
    rect(slide, .87, y, 11.57, .63,
         WHITE if i % 2 else LIGHT, stroke=FAINT, sw=.5)
    label(slide, name, 1.09, y+.13, 2.58, .37,
          16, BLUE, bold=True, font="Liberation Mono")
    label(slide, description, 3.92, y+.11, 8.19, .41,
          15, NAVY)
label(slide, "M2 added ppm.[ch] and matrix-first support; the ec_recover() baseline stays separate.",
      .88, 6.68, 11.55, .31, 15, SLATE)


# 22 — Programs, experiments, inputs and outputs.
slide = base("file reference", "Drivers, tests, data and results", 22)
sub(slide, "Programs exercise the shared library; CSVs and plots are generated artifacts.")
rect(slide, .86, 2.23, 5.7, .43, NAVY)
label(slide, "Programs / tests", 1.06, 2.29, 5.21, .31, 15, WHITE, bold=True)
left_rows = [
    ("main.c", "Paper-example baseline demo"),
    ("sweep.c", "M1 full-parameter work sweep"),
    ("benchmark.c", "M1 sequential encode/decode timing"),
    ("ppm_sweep.c", "M2 SD work sweep at T=1 and 4"),
    ("ppm_benchmark.c", "M2 baseline vs PPM timing"),
    ("tests/test_*.c", "GF, SD, coefficients, PPM tests"),
]
for i, (name, desc) in enumerate(left_rows):
    y = 2.67+i*.59
    rect(slide, .86, y, 5.7, .57, WHITE if i%2 else LIGHT)
    label(slide, name, 1.05, y+.09, 2.41, .34, 14, BLUE,
          bold=True, font="Liberation Mono")
    label(slide, desc, 3.49, y+.08, 2.91, .39, 13, NAVY)
rect(slide, 6.8, 2.23, 5.65, .43, NAVY)
label(slide, "Inputs / outputs", 7.01, 2.29, 5.18, .31, 15, WHITE, bold=True)
right_rows = [
    ("data/FAST-*", "Published coding coefficients"),
    ("results/sweep_*.csv", "M1 work measurements"),
    ("results/benchmark_*.csv", "M1 timings"),
    ("results/ppm_*.csv", "M2 work and scaling trials"),
    ("results/plots/", "Presentation charts (PNG + SVG)"),
    ("Makefile + scripts/", "Build, runs and plot generator"),
]
for i, (name, desc) in enumerate(right_rows):
    y = 2.67+i*.59
    rect(slide, 6.8, y, 5.65, .57, WHITE if i%2 else LIGHT)
    label(slide, name, 7.0, y+.09, 2.84, .34, 13, BLUE,
          bold=True, font="Liberation Mono")
    label(slide, desc, 9.88, y+.08, 2.38, .39, 12, NAVY)
rect(slide, .86, 6.41, 11.59, .44, LIGHT)
label(slide, "make run  ·  make benchmark  ·  make ppm-sweep  ·  make ppm-benchmark  ·  make plots",
      1.06, 6.48, 11.13, .3, 15, SLATE, align="center")


# 23 — Concrete M1-to-M2 code transition; excerpts preserve the source order.
slide = base("source excerpt", "From the M1 decoder to PPM jobs", 23)
sub(slide, "The baseline solves one F; PPM first identifies independent physical rows.")
rect(slide, .85, 2.23, 5.75, 3.72, LIGHT)
rect(slide, 6.78, 2.23, 5.7, 3.72, LIGHT)
label(slide, "M1  /  src/codec.c", 1.1, 2.45, 5.18, .42,
      18, BLUE, bold=True)
label(slide, "M2  /  src/ppm.c", 7.02, 2.45, 5.19, .42,
      18, ORANGE, bold=True)
label(slide, "ns = ec_split(H, faulty, nf, &F, &S, surviving);\n"
      "if (mat_invert(&F, gf, &Finv) != 0) goto done;\n"
      "gf_count_reset();\n"
      "if (ec_decode_normal(&Finv, &S, faulty,\n"
      "                     surviving, stripe,\n"
      "                     sector_bytes, gf) != 0)\n"
      "    goto done;",
      1.11, 3.07, 5.17, 2.65, 11, NAVY,
      font="Liberation Mono", valign="top")
label(slide, "if (row_faults[i] == m) {\n"
      "    row_is_independent[i] = 1;\n"
      "    p++;\n"
      "}\n\n"
      "jobs[i].rc = ec_decode_matrix_first(\n"
      "    &jobs[i].Finv, &jobs[i].S,\n"
      "    jobs[i].faulty, jobs[i].surviving,\n"
      "    stripe, sector_bytes, gf);",
      7.06, 3.04, 5.08, 2.75, 11, NAVY,
      font="Liberation Mono", valign="top")
label(slide, "M1:  T = S·BS; BF = F⁻¹·T", 1.04, 6.2, 5.46, .41,
      17, NAVY, bold=True)
label(slide, "M2:  G = F⁻¹·S; BF = G·BS", 6.99, 6.2, 5.27, .41,
      17, NAVY, bold=True)


# 24 — A literal inner loop, plus the dependency after it.
slide = base("source excerpt", "Inside the OpenMP recovery", 24)
sub(slide, "The loop owns disjoint jobs; leaving it is the barrier before the remainder.")
rect(slide, .86, 2.3, 7.27, 4.37, LIGHT)
label(slide, "src/ppm.c  ·  independent loop", 1.09, 2.48, 6.78, .38,
      18, BLUE, bold=True)
label(slide, "#pragma omp for schedule(static)\n"
      "for (i = 0; i < p; i++) {\n"
      "    gf_count_reset();\n"
      "    jobs[i].rc = ec_decode_matrix_first(\n"
      "        &jobs[i].Finv, &jobs[i].S,\n"
      "        jobs[i].faulty, jobs[i].surviving,\n"
      "        stripe, sector_bytes, gf);\n"
      "    jobs[i].mult_xors = gf_count_get();\n"
      "}",
      1.09, 3.02, 6.84, 3.34, 13, NAVY,
      font="Liberation Mono", valign="top")
rect(slide, 8.43, 2.3, 4.03, 1.44, PALE_BLUE)
label(slide, "src/gf.h  ·  thread-local", 8.67, 2.47, 3.58, .3,
      15, BLUE, bold=True)
label(slide, "extern _Thread_local uint64_t\n"
      "    gf_mult_xors_count;", 8.67, 2.89, 3.5, .58,
      11, NAVY, font="Liberation Mono", valign="top")
rect(slide, 8.43, 3.96, 4.03, 2.71, PALE_ORANGE)
label(slide, "After the barrier", 8.67, 4.16, 3.58, .38,
      18, ORANGE, bold=True)
label(slide, "ec_decode_normal(\n"
      "    &remainder.Finv,\n"
      "    &remainder.S, ...);", 8.67, 4.72, 3.53, 1.23,
      12, NAVY, font="Liberation Mono", valign="top")
label(slide, "Excerpt at right is shortened; full call uses remainder.faulty and remainder.surviving.",
      8.65, 6.09, 3.57, .44, 11, SLATE, valign="top")


# Keep the normal slide show ending at references. Backup pages remain editable
# and reachable in Impress/PowerPoint via the slide pane or navigator.
for appendix_slide in list(prs.slides)[18:]:
    appendix_slide._element.set("show", "0")


def embed_speaker_notes():
    guide = SPEAKER_GUIDE.read_text(encoding="utf-8")
    headers = list(re.finditer(r"^## Slide (\d{2}) — (.+)$", guide, re.M))
    if len(headers) != len(prs.slides):
        raise ValueError("The speaker guide must contain exactly one section per slide")
    for index, match in enumerate(headers):
        if int(match.group(1)) != index + 1:
            raise ValueError(f"Speaker notes out of order at section {index+1}")
        end = headers[index+1].start() if index+1 < len(headers) else len(guide)
        body = guide[match.end():end].split("\n---", 1)[0].strip()
        # Impress Notes view should show speech text, not Markdown decorations.
        body = body.replace("**", "").replace("`", "")
        prs.slides[index].notes_slide.notes_text_frame.text = (
            f"Slide {index+1:02d} — {match.group(2)}\n\n{body}"
        )


embed_speaker_notes()
prs.save(OUTPUT)
print(f"Wrote {OUTPUT} ({len(prs.slides)} slides with embedded notes)")
