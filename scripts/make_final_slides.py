#!/usr/bin/env python3
"""Create the final presentation and a matching, embedded speaker script.

Requires python-pptx. Run from any directory with
    python3 scripts/make_final_slides.py
"""
from pathlib import Path
import csv

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Inches, Pt

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/Final_Presentation.pptx"
NOTES = ROOT / "docs/Final_Presentation_Speaker_Notes.md"
DATA = ROOT / "results/milestone4/comparison_summary.csv"
with DATA.open(newline="") as f:
    DATA_ROWS = list(csv.DictReader(f))

def result(size, m, s):
    return next(r for r in DATA_ROWS if int(r["stripe_mib"]) == size
                and int(r["m"]) == m and int(r["s"]) == s)

prs = Presentation()
prs.slide_width, prs.slide_height = Inches(13.333), Inches(7.5)
BLANK = prs.slide_layouts[6]
NAVY, BLUE, TEAL = "1D3047", "315F8A", "147D87"
ORANGE, WHITE, BG, LIGHT = "B76843", "FFFFFF", "F7F9FB", "E7EEF4"
PALE_TEAL, PALE_ORANGE, SLATE, FAINT = "E2F2F1", "FAEDE6", "56677B", "CED9E2"
notes = []

def color(value):
    return RGBColor.from_string(value)

def shape(slide, x, y, w, h, fill, rounded=False, stroke=None):
    s = slide.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE if rounded else MSO_SHAPE.RECTANGLE,
                               Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid(); s.fill.fore_color.rgb = color(fill)
    if stroke:
        s.line.color.rgb = color(stroke); s.line.width = Pt(1)
    else:
        s.line.fill.background()
    for effect in s._element.xpath("p:style/a:effectRef"):
        effect.set("idx", "0")
    if rounded:
        s.adjustments[0] = 0.08
    return s

def text(slide, value, x, y, w, h, size=20, fill=NAVY, bold=False,
         align="left", valign="middle"):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.clear(); tf.word_wrap = True
    tf.margin_left = tf.margin_right = Inches(0.01)
    tf.margin_top = tf.margin_bottom = Inches(0.01)
    tf.vertical_anchor = {"top": MSO_ANCHOR.TOP, "middle": MSO_ANCHOR.MIDDLE,
                          "bottom": MSO_ANCHOR.BOTTOM}[valign]
    p = tf.paragraphs[0]; p.text = value
    p.alignment = {"left": PP_ALIGN.LEFT, "center": PP_ALIGN.CENTER,
                   "right": PP_ALIGN.RIGHT}[align]
    p.font.name = "Liberation Sans"; p.font.size = Pt(size)
    p.font.bold = bold; p.font.color.rgb = color(fill)
    return box

def arrow(slide, x, y, w=.38, h=.25, fill=BLUE):
    s = slide.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, Inches(x), Inches(y), Inches(w), Inches(h))
    s.fill.solid(); s.fill.fore_color.rgb = color(fill); s.line.fill.background()
    for effect in s._element.xpath("p:style/a:effectRef"):
        effect.set("idx", "0")

def line(slide, x1, y1, x2, y2, fill=FAINT, width=1.5):
    s = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    s.line.color.rgb = color(fill); s.line.width = Pt(width)
    return s

def dot(slide, x, y, size, fill):
    s = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(x-size/2), Inches(y-size/2), Inches(size), Inches(size))
    s.fill.solid(); s.fill.fore_color.rgb = color(fill); s.line.fill.background()
    return s

def base(section, title, subtitle=None, source=None):
    slide = prs.slides.add_slide(BLANK)
    slide.background.fill.solid(); slide.background.fill.fore_color.rgb = color(WHITE)
    text(slide, section.upper(), .65, .28, 11, .25, 11, TEAL, True)
    text(slide, title, .65, .65, 12.05, .58, 29, NAVY, True)
    shape(slide, .65, 1.40, 12.0, .016, FAINT)
    shape(slide, .65, 1.40, .7, .04, TEAL)
    if subtitle:
        text(slide, subtitle, .65, 1.56, 12, .34, 15.5, SLATE)
    text(slide, source or "gPPM  /  Parallel Computing", .65, 7.16, 11.5, .18, 9, SLATE)
    text(slide, f"{len(prs.slides):02}", 12.1, 7.13, .55, .22, 10, SLATE, align="right")
    return slide

def note(slide, title, script):
    slide.notes_slide.notes_text_frame.text = script.strip()
    notes.append((title, script.strip()))

def card(slide, x, y, w, h, kicker, heading, body, tint=LIGHT, accent=BLUE):
    shape(slide, x, y, w, h, tint, rounded=True)
    text(slide, kicker, x+.22, y+.20, w-.44, .25, 11, accent, True)
    text(slide, heading, x+.22, y+.57, w-.44, .45, 20, NAVY, True)
    text(slide, body, x+.22, y+1.15, w-.44, h-1.33, 15, SLATE, valign="top")

def bar(slide, label, value, maxvalue, x, y, width, fill, unit="ms", height=.30):
    text(slide, label, x, y-.04, 1.50, height+.08, 17, NAVY, True)
    shape(slide, x+1.65, y, width, height, "EEF2F5", rounded=True)
    shape(slide, x+1.65, y, width*value/maxvalue, height, fill, rounded=True)
    text(slide, f"{value:.2f} {unit}", x+1.74+width, y-.05, 1.1, height+.1, 15, NAVY, True)

def section_note():
    NOTES.write_text("# Final presentation — speaker notes\n\n"
                     "The same notes are embedded in the PowerPoint Notes pages. "
                     "Times and results refer to the final paper and retained benchmark data.\n\n" +
                     "\n\n".join(f"## Slide {i:02d} — {title}\n\n{script}"
                                   for i, (title, script) in enumerate(notes, 1)) + "\n")

# 1. Title
s = prs.slides.add_slide(BLANK)
s.background.fill.solid(); s.background.fill.fore_color.rgb = color(NAVY)
shape(s, .82, .87, 1.10, .07, "51BCC1")
text(s, "PARALLEL COMPUTING  /  FINAL PRESENTATION", .84, 1.15, 10, .28, 12, "9EDADE", True)
text(s, "Faster recovery,\non CPUs and GPUs", .82, 1.83, 9.3, 1.48, 42, WHITE, True)
text(s, "Cost-selected decoding for Sector–Disk erasure codes", .85, 3.49, 10.6, .42, 20, "D6E4ED")
text(s, "Tejas Singh Sodhi  ·  Arnav Verma  ·  Advay Satish  ·  Dhanush Kumar",
     .85, 5.45, 11.6, .38, 16, WHITE)
text(s, "BITS Pilani, K K Birla Goa Campus", .85, 5.92, 10, .31, 14, "B5C8D6")
for i, h in enumerate((.65, 1.03, 1.42, 1.83)):
    shape(s, 10.55+i*.49, 5.12-h, .28, h, ("5CAAB2", "5CAAB2", "8FC8CF", "D78B65")[i])
note(s, "Faster recovery, on CPUs and GPUs", """
Today we will continue from the first two milestones, which have already been presented.
The focus is what we added afterwards: cost-selected CPU recovery, GPU offloading,
and the final profiling and scaling study. The goal is to understand not just whether
parallelism helps, but which approach is appropriate for a particular recovery workload.
We will briefly recap the baseline, show the new design, and finish with measured
results and bottlenecks.
""")

# 2. Recap
s = base("Previously presented", "A quick recap: from sequential to partitioned recovery",
         "Both milestones solve the same recovery equations.")
card(s, .72, 2.13, 3.72, 2.70, "MILESTONE 1", "Sequential baseline",
     "Build the parity equations and recover missing sectors one matrix operation at a time.")
arrow(s, 4.58, 3.22, .47, .28)
card(s, 5.20, 2.13, 3.72, 2.70, "MILESTONE 2", "PPM + OpenMP",
     "Recover independent row groups in parallel, then solve the dependent remainder.", PALE_TEAL, TEAL)
arrow(s, 9.06, 3.22, .47, .28)
shape(s, 9.68, 2.13, 2.92, 2.70, PALE_ORANGE, rounded=True)
text(s, "WHAT COMES NEXT", 9.91, 2.34, 2.46, .25, 11, ORANGE, True)
text(s, "Less work.\nFaster kernels.\nGPU offload.", 9.91, 2.83, 2.45, 1.35, 20, NAVY, True)
text(s, "EXAMPLE FAILURE PATTERN", .83, 5.08, 2.5, .22, 10, SLATE, True)
for disk in range(4):
    text(s, f"D{disk+1}", .98 + disk*.36, 5.34, .31, .18, 9, SLATE, align="center")
    for row in range(4):
        failed = disk == 1 or (row == 3 and disk == 3)
        shape(s, .98 + disk*.36, 5.57 + row*.27, .29, .20, ORANGE if failed else LIGHT)
arrow(s, 2.65, 5.92, .40, .26)
shape(s, 3.28, 5.53, 3.33, .47, PALE_TEAL, rounded=True)
text(s, "3 independent row groups", 3.44, 5.61, 3.01, .28, 14, TEAL, True, align="center")
shape(s, 3.28, 6.12, 3.33, .47, PALE_ORANGE, rounded=True)
text(s, "1 dependent remainder", 3.44, 6.20, 3.01, .28, 14, ORANGE, True, align="center")
text(s, "Rows with only a disk loss can be solved first;\nthe extra sector makes the final row dependent.",
     7.12, 5.58, 5.30, .91, 16, SLATE)
note(s, "A quick recap", """
In Milestone 1, we implemented the sequential Sector–Disk recovery baseline.
The decoder builds the parity-check equations, separates failed sectors from survivors,
and applies matrix operations over a finite field to reconstruct the missing data.
In Milestone 2, we used the code structure to partition row-local failures into
independent groups. OpenMP workers can recover those groups concurrently, after which
the dependent remainder is solved. In the little example, one whole disk and one
additional sector are missing: three rows are independent and the fourth depends
on a stripe-wide equation. That is the hand-off to the new work: we keep
the same recovery problem, but choose the computation more carefully and optimize
how the arithmetic runs on CPUs and GPUs.
""")

# 3. Cost selection
s = base("Milestone 3  /  recovery design", "Choose the cheaper route before touching the data",
         "Equivalent recovery sequences can require different amounts of work.")
shape(s, .80, 2.17, 3.35, 2.35, LIGHT, rounded=True)
text(s, "NORMAL", 1.07, 2.40, 2.8, .28, 12, BLUE, True)
text(s, "Apply the two matrices\nto the data in sequence", 1.07, 2.92, 2.8, 1.00, 21, NAVY, True)
shape(s, 4.54, 2.17, 3.35, 2.35, PALE_TEAL, rounded=True)
text(s, "MATRIX-FIRST", 4.81, 2.40, 2.8, .28, 12, TEAL, True)
text(s, "Multiply the small matrices\nfirst; then apply to data", 4.81, 2.92, 2.8, 1.00, 21, NAVY, True)
arrow(s, 8.14, 3.18, .51, .30)
shape(s, 8.94, 2.17, 3.45, 2.35, NAVY, rounded=True)
text(s, "COST SELECTOR", 9.22, 2.40, 2.9, .28, 12, "9EDADE", True)
text(s, "Estimate work\n→ pick a sequence\n→ consider partitioning", 9.22, 2.85, 2.9, 1.13, 19, WHITE, True)
text(s, "Cost model: nonzero operations + extra cost for non-one multiplication", .82, 5.18, 11.7, .50, 19, SLATE)
text(s, "The model estimates arithmetic work, not scheduling or elapsed time.", .82, 5.78, 11.7, .38, 15, ORANGE)
note(s, "Choose the cheaper route", """
The first Milestone 3 change is about doing less work. The normal sequence applies
the survivor matrix to data, then applies the inverse failure matrix. Matrix-first
instead combines the small coefficient matrices before applying the result to the
large data regions. Both produce the same recovered sectors, but their operation
counts can differ. Our selector estimates the work from the number of nonzero
coefficients and the additional cost of multiplications by values other than one.
It also compares the partitioned approach with whole-matrix recovery. This is a
heuristic for arithmetic work, not a prediction of total runtime or thread overhead.
""")

# 4. CPU layers
s = base("Milestone 3  /  CPU optimization", "Three layers of optimization, one recovery goal",
         "Ablation study: cumulative gain over the single-threaded scalar baseline.",
         "Initial CPU layer study  ·  32 MiB  ·  m=2, s=1  ·  T=8")
for y, k, title, detail, tint, accent in [
    (2.06, "01", "Cost selection", "Choose normal, matrix-first, or partitioned recovery", LIGHT, BLUE),
    (3.10, "02", "SIMD arithmetic", "Process multiple field symbols per CPU instruction", PALE_TEAL, TEAL),
    (4.14, "03", "Parallel remainder", "Split remaining byte ranges across OpenMP workers", PALE_ORANGE, ORANGE),
]:
    shape(s, .78, y, 7.18, .85, tint, rounded=True)
    text(s, k, 1.00, y+.19, .49, .34, 14, accent, True)
    text(s, title, 1.64, y+.09, 2.55, .35, 19, NAVY, True)
    text(s, detail, 1.64, y+.46, 5.99, .25, 13, SLATE)
shape(s, 8.30, 2.06, 4.23, 2.93, NAVY, rounded=True)
text(s, "EXAMPLE: GF(2⁸)", 8.61, 2.34, 3.6, .27, 12, "9EDADE", True)
for i, (label, gain, bar_color) in enumerate((("SELECT", 1.53, "86B1D0"),
                                               ("+ SIMD", 15.06, "67C5C4"),
                                               ("+ REM.", 25.42, "EAA985"))):
    y = 2.97 + i*.58
    text(s, label, 8.58, y-.02, .92, .30, 11, "D6E4ED", True)
    shape(s, 9.55, y+.02, 2.02, .23, "43566A", rounded=True)
    shape(s, 9.55, y+.02, 2.02*gain/27, .23, bar_color, rounded=True)
    text(s, f"{gain:.2f}×", 11.68, y-.02, .68, .30, 12, WHITE, True, align="right")
text(s, "The full nine-workload range at T=8 is 1.69×–34.85×.",
     .82, 5.59, 11.5, .42, 18, SLATE)
note(s, "Three layers of CPU optimization", """
The CPU implementation adds three complementary layers. First, cost selection
reduces estimated arithmetic work. Second, SIMD kernels operate on multiple field
symbols at once. Third, we parallelize the dependent remainder by dividing data
into disjoint byte ranges; each worker applies the same transformation to its range.
In the initial layer study, this specific GF two-to-the-eight case reaches 1.53 times
the scalar baseline after cost selection, 15.06 times with SIMD, and 25.42 times
with the parallel remainder at eight threads. These are cumulative ratios to the
single-threaded scalar normal baseline, not three independent speedups. Across all
nine configurations the final range is 1.69 to 34.85 times.
""")

# 5. Thread scaling
r = result(32, 2, 3)
s = base("Milestone 4  /  CPU scaling", "More threads are not always faster",
         "One workload: GF(2³²), m=2, s=3, 32 MiB  ·  lower latency is better",
         "Final experiment  ·  medians of 10 validated trials  ·  CPU L3")
for i, t in enumerate((1, 2, 4, 8)):
    bar(s, f"{t} thread" + ("" if t == 1 else "s"), float(r[f"cpu_{t}t_ms"]), 50,
        .82, 2.34+i*.73, 5.30, TEAL if t == 2 else BLUE, height=.34)
shape(s, 9.30, 2.26, 3.18, 2.80, PALE_TEAL, rounded=True)
text(s, "BEST OF 1 / 2 / 4 / 8", 9.56, 2.53, 2.66, .28, 11, TEAL, True)
text(s, "2 threads", 9.56, 3.01, 2.66, .48, 25, NAVY, True)
text(s, "32.12 ms versus\n37.49 ms at 8 threads", 9.56, 3.69, 2.66, .74, 17, SLATE)
text(s, "Across nine 32 MiB workloads, the best tested setting delivered 1.37×–2.90× over 1 thread.",
     .82, 6.00, 11.75, .48, 17, NAVY)
note(s, "More threads are not always faster", """
The final experiment compares one, two, four and eight threads within the same
measurement run. This example uses a 32 MiB stripe, two failed disks and three
additional sector failures, in the 32-bit field. The two-thread median is 32.12
milliseconds; the eight-thread median is 37.49 milliseconds. So adding workers
does not guarantee an improvement. The best of the four settings gives between
1.37 and 2.90 times the one-thread L3 speed across the nine 32 MiB workloads.
These are medians from ten trials, and the best setting is selected from those same
trials. Scheduling, the heterogeneous CPU, calibration and variation can affect the ranking.
""")

# 6. GPU design
s = base("GPU implementation", "Keep planning on the CPU; move region work to the GPU",
         "The timing includes the complete recovery path—not just a kernel.")
for x, title, body, tint in [
    (.73, "1  HOST", "Prepare matrices\nand select sequence", LIGHT),
    (3.69, "2  TRANSFER", "Allocate buffers\nand copy stripe", PALE_TEAL),
    (6.65, "3  GPU", "Parallel groups, then\ndependent remainder", PALE_ORANGE),
    (9.61, "4  RETURN", "Copy recovered stripe\nback; release buffers", LIGHT),
]:
    shape(s, x, 2.25, 2.68, 2.10, tint, rounded=True)
    text(s, title, x+.19, 2.49, 2.30, .32, 13, TEAL if x == 3.69 else BLUE, True)
    text(s, body, x+.19, 3.05, 2.30, .93, 18, NAVY, True)
for x in (3.43, 6.39, 9.35):
    arrow(s, x, 3.12, .20, .24)
text(s, "Two arithmetic variants", .82, 5.08, 3.35, .37, 18, NAVY, True)
shape(s, 4.15, 4.99, 3.52, .61, LIGHT, rounded=True)
text(s, "Direct shift-and-reduce", 4.31, 5.09, 3.20, .34, 16, BLUE, True, align="center")
shape(s, 7.86, 4.99, 4.00, .61, PALE_TEAL, rounded=True)
text(s, "Lookup-table multiplication", 8.03, 5.09, 3.66, .34, 16, TEAL, True, align="center")
text(s, "Independent groups use separate CUDA streams; a dependency barrier precedes the remainder.",
     .82, 6.05, 11.7, .46, 15, SLATE)
note(s, "GPU recovery flow", """
On the GPU path, the host still prepares the matrices and selects the recovery
sequence. Each recovery allocates device memory, copies the stripe and required
tables, runs the matrix-vector kernels, then copies the stripe back and frees
resources. Independent groups are submitted to separate CUDA streams. They finish
before the dependent remainder starts. Each CUDA thread handles one output symbol.
We compare direct shift-and-reduce multiplication with a table-based variant. The
direct variant is still massively parallel on the GPU; it is not a single-threaded
GPU implementation. Importantly, our reported GPU time includes preparation,
allocation, transfers, synchronization and cleanup, rather than kernel time alone.
""")

# 7. Experiment
s = base("Milestone 4  /  evaluation", "A fairer final comparison: same workload, shuffled order",
         "Measure recovery end to end; validate the output after every trial.")
card(s, .78, 2.15, 3.73, 2.65, "SYSTEM", "CPU + RTX 4050",
     "Intel Core Ultra 9 185H\nRTX 4050 Laptop GPU\nCUDA 13.0", LIGHT, BLUE)
card(s, 4.80, 2.15, 3.73, 2.65, "VARIANTS", "4 CPU + 2 GPU",
     "CPU: 1, 2, 4 and 8 threads\nGPU: direct and table\n10 measured trials each", PALE_TEAL, TEAL)
card(s, 8.82, 2.15, 3.73, 2.65, "COVERAGE", "900 / 900 pass",
     "Nine cases at 32 MiB\nThree cases at 8 and 128 MiB\nBytes + checksum + syndrome", PALE_ORANGE, ORANGE)
text(s, "One warm-up per variant  ·  Variant order shuffled in each round  ·  15 independent groups",
     .82, 5.43, 11.7, .45, 17, SLATE)
text(s, "One system and one session; stripe sizes were run in separate processes.",
     .82, 6.12, 11.7, .32, 14, ORANGE)
note(s, "The final evaluation", """
For the final evaluation, we compared four CPU thread settings with both GPU
variants using the same encoded data and failure pattern within a configuration.
The platform is an Intel Core Ultra 9 185H and an RTX 4050 Laptop GPU. We ran all
nine combinations of one to three failed disks and one to three additional sector
failures at 32 MiB. At eight and 128 MiB we tested the three cases with two failed
disks. Each variant warmed once and then ran ten measured trials; order was shuffled
within each round. All 900 trials passed recovered-byte, checksum and parity
syndrome validation. This is one machine and one session, and the three stripe-size
suites were run separately in a fixed order.
""")

# 8. Comparison
s = base("Milestone 4  /  CPU vs GPU", "At 32 MiB, the winner depends on the field",
         "m=2  ·  Best CPU = lowest median among 1, 2, 4 and 8 threads  ·  lower is better",
         "Final experiment  ·  median end-to-end recovery  ·  GPU transfer + setup included")
shape(s, .83, 2.08, .22, .22, BLUE); text(s, "Best tested CPU", 1.18, 2.01, 2.35, .34, 15, BLUE, True)
shape(s, 4.07, 2.08, .22, .22, TEAL); text(s, "GPU table", 4.43, 2.01, 2.0, .34, 15, TEAL, True)
for i, (field, ss) in enumerate((("GF(2⁸)  /  s=1", 1), ("GF(2¹⁶)  /  s=2", 2), ("GF(2³²)  /  s=3", 3))):
    row = result(32, 2, ss); y = 2.64 + i*1.13
    text(s, field, .85, y, 2.3, .35, 17, NAVY, True)
    for j, (name, hue) in enumerate((("best_cpu_ms", BLUE), ("gpu_table_ms", TEAL))):
        val = float(row[name]); yy = y + j*.37
        shape(s, 3.22, yy, 6.42 * val / 40, .24, hue, rounded=True)
        text(s, f"{val:.2f} ms", 3.35 + 6.42*val/40, yy-.05, 1.35, .34, 14, NAVY, True)
shape(s, 10.88, 4.66, 1.65, .75, PALE_TEAL, rounded=True)
text(s, "1.31×", 10.97, 4.72, 1.47, .35, 21, TEAL, True, align="center")
text(s, "GPU gain", 10.97, 5.06, 1.47, .22, 11, TEAL, align="center")
text(s, "The table GPU is faster in all three GF(2³²) cases; the CPU wins in the smaller fields.",
     .84, 6.39, 11.7, .39, 16, NAVY)
note(s, "CPU versus GPU at 32 MiB", """
Here we compare end-to-end medians from the final 32 MiB experiment for the
two-disk-failure cases. Blue is the fastest median among the four tested CPU
thread counts; teal is the GPU table variant. The CPU is much faster for the
8-bit and 16-bit fields. In the 32-bit field, the best tested CPU takes 32.12
milliseconds and the table GPU takes 24.48 milliseconds, a 1.31 times gain.
The other two 32-bit cases yield 1.25 and 1.39 times. This is not a claim that
the GPU always wins, or that we exhaustively tuned the CPU. The field width and
the number of extra sector failures also change together in these workloads.
""")

# 9. Size scaling
s = base("Milestone 4  /  size scaling", "The winner changes as the stripe grows",
         "GF(2³²), m=2, s=3  ·  Best tested CPU vs GPU table  ·  median latency",
         "Final experiment  ·  separate runs in fixed 32 → 8 → 128 MiB order")
text(s, "Latency (ms)", .89, 2.05, 1.55, .28, 13, SLATE, True)
shape(s, 8.54, 2.06, .20, .20, BLUE); text(s, "Best CPU", 8.85, 2.00, 1.35, .30, 14, BLUE, True)
shape(s, 10.27, 2.06, .20, .20, TEAL); text(s, "GPU table", 10.58, 2.00, 1.65, .30, 14, TEAL, True)
xs = (3.14, 6.54, 9.94)
top, bottom, max_ms = 2.49, 5.48, 100.0
for tick in (0, 20, 40, 60, 80, 100):
    y = bottom - tick/max_ms*(bottom-top)
    line(s, 1.65, y, 11.20, y, "E3EAF0", .7)
    text(s, str(tick), .94, y-.12, .51, .24, 11, SLATE, align="right")
for x, size in zip(xs, (8, 32, 128)):
    text(s, f"{size} MiB", x-.60, 5.62, 1.20, .30, 15, NAVY, True, align="center")
for field, hue in (("best_cpu_ms", BLUE), ("gpu_table_ms", TEAL)):
    values = [float(result(size, 2, 3)[field]) for size in (8,32,128)]
    points = [(x, bottom-v/max_ms*(bottom-top)) for x,v in zip(xs,values)]
    for (x1,y1),(x2,y2) in zip(points,points[1:]):
        line(s, x1,y1,x2,y2,hue,3)
    for i, ((x,y),v) in enumerate(zip(points,values)):
        dot(s, x,y,.16,hue)
        yy = y-.43 if field == "best_cpu_ms" else y+.12
        if i == 2 and field == "gpu_table_ms": yy = y-.41
        text(s, f"{v:.2f}", x+.14, yy, .98, .28, 13, hue, True)
text(s, "Log-spaced sizes; lines guide the eye. One session—no universal crossover established.",
     .84, 6.42, 11.65, .38, 15, SLATE)
note(s, "Stripe size changes the comparison", """
The 32-bit-field example gives a useful counterpoint. At eight MiB, the GPU table
variant takes 8.47 milliseconds compared with 21.88 for the best tested CPU.
At 32 MiB, the GPU still wins: 24.48 against 32.12 milliseconds. At 128 MiB,
the CPU takes 59.47 milliseconds and the GPU takes 83.26 milliseconds, so the
CPU wins that measured case by about 1.40 times. We should not claim an exact
hardware crossover point. These size suites ran in separate processes in the
fixed order 32, 8, then 128 MiB, with fresh calibration. Run order, scheduling,
system state, and selecting the best CPU thread count from the same trials can
all affect the observed ranking.
""")

# 10. Profiling
s = base("Milestone 4  /  profiling", "Where is the work concentrated?",
         "Recovery-only profiles, m=2 and 32 MiB  ·  profiling runs are separate from benchmark timing",
         "Callgrind: 1 CPU thread  ·  Nsight Systems: one GPU-table trace  ·  GF(2³²)")
shape(s, .82, 2.15, 5.67, 3.40, LIGHT, rounded=True)
text(s, "CPU  /  INSTRUCTIONS", 1.09, 2.44, 5.1, .29, 12, BLUE, True)
text(s, "58.2%", 1.08, 2.97, 2.15, .72, 38, NAVY, True)
text(s, "in scalar GF(2³²) multiply", 3.05, 3.12, 3.04, .42, 16, SLATE)
for x, label in ((1.10,"COEFFICIENT"), (2.84,"BUILD TABLE"), (4.58,"SIMD APPLY")):
    shape(s, x, 4.04, 1.39, .50, WHITE, rounded=True)
    text(s, label, x+.04, 4.14, 1.31, .27, 10, BLUE, True, align="center")
for x in (2.54, 4.28): arrow(s, x, 4.20, .24, .18, BLUE)
text(s, "Table construction repeats inside the region kernel.", 1.10, 4.79, 4.97, .40, 14, NAVY)
shape(s, 6.82, 2.15, 5.67, 3.40, PALE_TEAL, rounded=True)
text(s, "GPU  /  TRACE", 7.09, 2.44, 5.1, .29, 12, TEAL, True)
text(s, "11.80 ms", 7.08, 2.95, 2.48, .55, 25, NAVY, True)
text(s, "kernel timeline union", 9.52, 3.03, 2.60, .38, 15, SLATE)
text(s, "10.12 ms", 7.08, 3.62, 2.48, .55, 25, NAVY, True)
text(s, "summed copy intervals", 9.52, 3.70, 2.60, .38, 15, SLATE)
for x, label in ((7.10,"COPY"), (8.82,"KERNEL"), (10.54,"SYNC")):
    shape(s, x, 4.31, 1.37, .48, WHITE, rounded=True)
    text(s, label, x+.04, 4.41, 1.29, .26, 11, TEAL, True, align="center")
for x in (8.51, 10.23): arrow(s, x, 4.46, .24, .17, TEAL)
text(s, "17 kernels  ·  52 copies  ·  allocation per recovery", 7.09, 4.90, 4.93, .32, 13, NAVY)
text(s, "Instruction shares ≠ elapsed-time shares. GPU kernel, copy and host-wait intervals can overlap.",
     .84, 5.98, 11.7, .50, 16, ORANGE)
note(s, "Profiling and bottlenecks", """
We profiled recovery separately from the native timing experiment. On the CPU,
Callgrind counted instructions for a one-thread GF two-to-the-thirty-two recovery.
The scalar field-multiply routine accounts for 58.2 percent of instructions; 57.2
percentage points are attributed to calls from the SIMD region kernel, where
coefficient tables are rebuilt. Reusing these tables is a concrete candidate for
future work. These instruction shares are not native time shares. For the GPU
table variant in the same workload, Nsight records a kernel timeline union of
11.80 milliseconds and summed copy intervals of 10.12 milliseconds, with 17
kernels and 52 copies. These categories and host synchronization can overlap, so
we cannot add them to obtain total latency. The traces point to computation,
transfer and repeated setup as targets; they do not prove a single root cause.
""")

# 11. Close
s = base("Conclusions", "The best recovery path is workload-dependent",
         "A practical decision needs algorithm, arithmetic and execution overhead together.")
for y, n, heading, body, tint, accent in [
    (2.07, "01", "Reduce work first", "Cost selection and partitioning choose how to recover.", LIGHT, BLUE),
    (3.23, "02", "Tune the CPU", "SIMD and threads help, but scaling is not monotonic.", PALE_TEAL, TEAL),
    (4.39, "03", "Offload selectively", "The GPU wins some cases; transfer and setup matter.", PALE_ORANGE, ORANGE),
]:
    shape(s, .83, y, 8.06, .93, tint, rounded=True)
    text(s, n, 1.08, y+.27, .46, .32, 14, accent, True)
    text(s, heading, 1.75, y+.10, 3.14, .34, 19, NAVY, True)
    text(s, body, 1.75, y+.52, 6.85, .27, 14, SLATE)
shape(s, 9.27, 2.07, 3.18, 3.25, NAVY, rounded=True)
text(s, "NEXT", 9.55, 2.38, 2.65, .28, 12, "9EDADE", True)
text(s, "Reuse tables\nand GPU buffers", 9.55, 2.93, 2.62, .94, 21, WHITE, True)
text(s, "Repeat across sizes,\nlayouts and sessions.", 9.55, 4.25, 2.55, .65, 15, "D6E4ED")
text(s, "Thank you", .85, 6.22, 5.5, .43, 22, NAVY, True)
note(s, "Conclusions", """
The main takeaway is to choose the recovery path for the workload rather than
assuming that more parallelism or GPU offload always wins. Cost selection and
partitioning reduce or reorganize the work; SIMD and threads accelerate the CPU
implementation; and GPU offload helps some, but not all, of the tested cases.
The final profiling points to repeated CPU coefficient-table construction and
GPU kernel, transfer, and setup activity as practical optimization targets.
Next we would test coefficient-table reuse and persistent GPU buffers or batching,
then repeat the study across more stripe sizes, failure layouts and sessions.
All reported measurements and the limitations are documented in the final paper.
Thank you; we are happy to take questions.
""")

assert len(prs.slides) == len(notes) == 11
prs.save(OUTPUT)
section_note()
print(f"Wrote {OUTPUT.relative_to(ROOT)} and {NOTES.relative_to(ROOT)} ({len(notes)} slides)")
