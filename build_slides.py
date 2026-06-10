#!/usr/bin/env python3
"""Generate the RealWaterSimulator course presentation as a .pptx.

Design: deep-ocean theme, minimal text, native editable vector visuals
(sine-wave motifs + flow diagrams). 16:9. Built for a 5-minute talk.
"""
import math
from pptx import Presentation
from pptx.util import Inches, Pt, Emu
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN, MSO_ANCHOR
from pptx.enum.shapes import MSO_SHAPE, MSO_CONNECTOR
from pptx.enum.text import MSO_AUTO_SIZE
from pptx.oxml.ns import qn

# ---------------------------------------------------------------- palette
DEEP   = RGBColor(0x06, 0x12, 0x22)   # near-black navy
DEEP2  = RGBColor(0x0B, 0x22, 0x3C)   # deep ocean blue
MID    = RGBColor(0x12, 0x33, 0x52)   # mid panel blue
INK    = RGBColor(0xEA, 0xF2, 0xFA)   # off-white
MUTE   = RGBColor(0x8B, 0xA3, 0xBC)   # slate
AQUA   = RGBColor(0x57, 0xC7, 0xE8)   # sparkle cyan
TEAL   = RGBColor(0x2A, 0xC0, 0xAE)   # foam teal
GOLD   = RGBColor(0xF3, 0xC5, 0x6A)   # sun glint
BOXBG  = RGBColor(0x10, 0x2A, 0x47)   # flow-box fill
BOXLN  = RGBColor(0x24, 0x4B, 0x72)   # flow-box border

EMU_IN = 914400
SW, SH = 13.333, 7.5

prs = Presentation()
prs.slide_width  = Inches(SW)
prs.slide_height = Inches(SH)
BLANK = prs.slide_layouts[6]

# ---------------------------------------------------------------- helpers
def slide():
    return prs.slides.add_slide(BLANK)

def _set_gradient(fill, c1, c2, angle_deg):
    fill.gradient()
    try:
        fill.gradient_angle = angle_deg
    except Exception:
        pass
    stops = fill.gradient_stops
    stops[0].position = 0.0
    stops[0].color.rgb = c1
    stops[1].position = 1.0
    stops[1].color.rgb = c2

def bg(s, c1=DEEP2, c2=DEEP, angle=90):
    r = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, 0, 0, prs.slide_width, prs.slide_height)
    r.line.fill.background()
    r.shadow.inherit = False
    _set_gradient(r.fill, c1, c2, angle)
    return r

def no_line(sp):
    sp.line.fill.background()
    sp.shadow.inherit = False
    return sp

def text(s, txt, x, y, w, h, size, color=INK, bold=False, italic=False,
         align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP, font="Calibri Light",
         spacing=None, line_spacing=None):
    tb = s.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = tb.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = anchor
    tf.margin_left = 0; tf.margin_right = 0; tf.margin_top = 0; tf.margin_bottom = 0
    lines = txt.split("\n")
    for i, ln in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        if line_spacing: p.line_spacing = line_spacing
        r = p.add_run(); r.text = ln
        f = r.font
        f.size = Pt(size); f.bold = bold; f.italic = italic
        f.name = font; f.color.rgb = color
        if spacing is not None:
            _letter_spacing(r, spacing)
    return tb

def _letter_spacing(run, pts):
    run.font._rPr.set('spc', str(int(pts * 100)))

def kicker(s, txt, x=0.92, y=0.62):
    # small accent label + underline rule
    text(s, txt.upper(), x, y, 8, 0.4, 13, AQUA, bold=True,
         font="Calibri", spacing=2.5)

def title(s, txt, x=0.92, y=1.02, w=11.5, size=33, color=INK):
    text(s, txt, x, y, w, 1.0, size, color, bold=False, font="Calibri Light")

def rule(s, x, y, w, color=AQUA, weight=2.2):
    ln = s.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(x), Inches(y),
                                Inches(x+w), Inches(y))
    ln.line.color.rgb = color
    ln.line.width = Pt(weight)
    ln.shadow.inherit = False
    return ln

def box(s, x, y, w, h, fill=BOXBG, line=BOXLN, lw=1.25, round=True):
    shp = s.shapes.add_shape(
        MSO_SHAPE.ROUNDED_RECTANGLE if round else MSO_SHAPE.RECTANGLE,
        Inches(x), Inches(y), Inches(w), Inches(h))
    shp.fill.solid(); shp.fill.fore_color.rgb = fill
    if line is None:
        shp.line.fill.background()
    else:
        shp.line.color.rgb = line; shp.line.width = Pt(lw)
    shp.shadow.inherit = False
    if round:
        try: shp.adjustments[0] = 0.10
        except Exception: pass
    return shp

def flowbox(s, x, y, w, h, head, sub, num=None, head_color=AQUA):
    box(s, x, y, w, h)
    pad = 0.18
    if num is not None:
        text(s, num, x+pad, y+0.12, w-2*pad, 0.4, 12, MUTE, bold=True, font="Calibri")
    text(s, head, x+pad, y+0.40, w-2*pad, 0.5, 16, head_color, bold=True, font="Calibri")
    text(s, sub, x+pad, y+0.92, w-2*pad, h-1.0, 11.5, MUTE, font="Calibri Light",
         line_spacing=1.0)

def arrow(s, x, y, w=0.42, color=AQUA):
    a = s.shapes.add_shape(MSO_SHAPE.CHEVRON, Inches(x), Inches(y), Inches(w), Inches(0.34))
    a.fill.solid(); a.fill.fore_color.rgb = color
    a.line.fill.background(); a.shadow.inherit = False
    return a

def sine(s, x, y, w, amp, cycles, color=AQUA, weight=2.5, n=140, phase=0.0):
    """Open polyline sine wave. x,y,w,amp in inches; y is vertical center."""
    sx = Emu(int(x*EMU_IN)); sy = Emu(int(y*EMU_IN))
    pts = []
    for i in range(n+1):
        t = i/n
        px = (x + t*w)*EMU_IN
        py = (y - amp*math.sin(phase + t*cycles*2*math.pi))*EMU_IN
        pts.append((Emu(int(px)), Emu(int(py))))
    fb = s.shapes.build_freeform(pts[0][0], pts[0][1], scale=1.0)
    fb.add_line_segments(pts[1:], close=False)
    shp = fb.convert_to_shape()
    shp.fill.background()
    shp.line.color.rgb = color
    shp.line.width = Pt(weight)
    shp.shadow.inherit = False
    try:
        shp.line.line_join_round = True
    except Exception:
        pass
    return shp

def dot(s, cx, cy, d, color):
    o = s.shapes.add_shape(MSO_SHAPE.OVAL, Inches(cx-d/2), Inches(cy-d/2),
                           Inches(d), Inches(d))
    o.fill.solid(); o.fill.fore_color.rgb = color
    o.line.fill.background(); o.shadow.inherit = False
    return o

def page(s, n):
    text(s, f"{n:02d}", SW-1.05, SH-0.62, 0.7, 0.4, 11, MUTE, align=PP_ALIGN.RIGHT, font="Calibri")
    text(s, "RealWaterSimulator", 0.92, SH-0.62, 5, 0.4, 11, MUTE, font="Calibri")

# ================================================================ SLIDE 1 — Title
s = slide(); bg(s, DEEP2, DEEP, 90)
# layered horizon waves, low and subtle
sine(s, 0.0, 5.55, SW, 0.16, 3.0, color=RGBColor(0x1E,0x55,0x7A), weight=2.0, phase=0.4)
sine(s, 0.0, 5.95, SW, 0.22, 2.3, color=RGBColor(0x2A,0x6E,0x9C), weight=2.2, phase=1.1)
sine(s, 0.0, 6.42, SW, 0.30, 1.7, color=AQUA, weight=2.8, phase=0.0)
# sun glint
dot(s, 10.7, 2.05, 0.9, RGBColor(0x18,0x39,0x52))
dot(s, 10.7, 2.05, 0.5, GOLD)
text(s, "Real-Time Water Simulator", 0.92, 2.35, 11.5, 1.3, 54, INK, font="Calibri Light")
rule(s, 1.0, 3.55, 2.1, AQUA, 2.5)
text(s, "A GPU ocean you can sail on", 0.92, 3.75, 11, 0.7, 22, AQUA, font="Calibri Light")
text(s, "C++  ·  OpenGL 4.6 compute shaders  ·  live & interactive at 100+ FPS",
     0.95, 4.55, 11, 0.5, 15, MUTE, font="Calibri")
text(s, "Computer Graphics", 0.95, 6.85, 6, 0.4, 13, MUTE, font="Calibri")

# ================================================================ SLIDE 2 — What we built
s = slide(); bg(s); kicker(s, "Overview"); title(s, "One interactive world, four moving parts")
rule(s, 0.95, 1.78, 1.4)
items = [
    ("Ocean",  "A physically-based sea, rebuilt on the GPU every frame", AQUA),
    ("Boats",  "Three vessels that float, tilt, and leave wakes", TEAL),
    ("Weather","Rain, sun, and wind — calm to storm, on sliders", GOLD),
    ("Coast",  "A scanned cliff ring and rock enclosing the bay", RGBColor(0x9A,0xB4,0xCC)),
]
cw, gap, x0, y0, ch = 2.86, 0.27, 0.95, 2.35, 3.3
for i,(h,sub,c) in enumerate(items):
    x = x0 + i*(cw+gap)
    box(s, x, y0, cw, ch, fill=BOXBG, line=BOXLN)
    # accent top bar
    bar = box(s, x, y0, cw, 0.12, fill=c, line=None);
    text(s, h, x+0.22, y0+0.42, cw-0.44, 0.6, 21, INK, bold=False)
    text(s, sub, x+0.22, y0+1.15, cw-0.44, ch-1.2, 14, MUTE, font="Calibri Light", line_spacing=1.1)
text(s, "The ocean is the core; everything else proves it is real and usable.",
     0.95, 6.05, 11.5, 0.6, 16, INK, italic=True, font="Calibri Light")
page(s, 2)

# ================================================================ SLIDE 3 — The idea
s = slide(); bg(s); kicker(s, "The idea"); title(s, "An ocean is a sum of waves")
rule(s, 0.95, 1.78, 1.4)
# three component waves -> one complex wave
base_x, ww = 1.1, 3.1
ys = 3.05
for i,(amp,cyc,col) in enumerate([(0.16,4,MUTE),(0.22,2.5,MUTE),(0.12,6,MUTE)]):
    yy = 2.55 + i*0.62
    sine(s, base_x, yy, ww, amp, cyc, color=col, weight=2.0)
text(s, "thousands of simple waves", base_x, 4.5, ww, 0.4, 13, MUTE, align=PP_ALIGN.CENTER, font="Calibri")
# plus / sigma
text(s, "Σ", base_x+ww+0.15, 2.7, 1.0, 1.4, 60, AQUA, align=PP_ALIGN.CENTER, font="Calibri Light")
arrow(s, base_x+ww+0.35, 3.25, 0.5, AQUA)
# result wave
rx = base_x+ww+1.55
sine(s, rx, 3.15, 4.7, 0.40, 2.0, color=AQUA, weight=3.2, phase=0.0)
sine(s, rx, 3.15, 4.7, 0.40, 2.0, color=AQUA, weight=3.2, phase=0.0)  # emphasis
text(s, "one ocean surface", rx, 4.5, 4.7, 0.4, 13, AQUA, align=PP_ALIGN.CENTER, font="Calibri")
text(s, "The mix of wave sizes comes from a real wind-wave spectrum  (Elfouhaily / IFREMER).",
     0.95, 5.55, 11.5, 0.5, 17, INK, font="Calibri Light")
text(s, "Bigger wind  →  bigger, longer waves.   We rebuild the whole surface every frame on the GPU.",
     0.95, 6.15, 11.5, 0.5, 14, MUTE, font="Calibri")
page(s, 3)

# ================================================================ SLIDE 4 — GPU pipeline
s = slide(); bg(s); kicker(s, "How the ocean is built")
title(s, "Five GPU stages, every frame")
rule(s, 0.95, 1.78, 1.4)
stages = [
    ("RECIPE","Which waves exist, and how tall — from the spectrum"),
    ("CLOCK","Advance each wave in time at its own speed"),
    ("MIX","Combine into this frame's wave field (+ choppiness)"),
    ("FFT","Sum all waves into a real surface — inverse FFT"),
    ("SURFACE","Height map + surface tilt + foam, drawn on a grid"),
]
n=len(stages); bw=2.18; bh=1.95; gap=0.20; x0=0.95; y0=2.25
for i,(h,sub) in enumerate(stages):
    x = x0 + i*(bw+gap)
    flowbox(s, x, y0, bw, bh, h, sub, num=f"{i+1}")
    if i < n-1:
        arrow(s, x+bw+0.0, y0+bh/2-0.17, 0.20, AQUA)
# two highlights strip
hy = 4.75
text(s, "Two parts worth highlighting", 0.95, hy, 6, 0.4, 14, AQUA, bold=True, font="Calibri")
box(s, 0.95, hy+0.5, 5.75, 1.45, fill=MID, line=BOXLN)
text(s, "Stockham FFT", 1.2, hy+0.66, 5.3, 0.4, 16, TEAL, bold=True, font="Calibri")
text(s, "One step per dispatch instead of one big scratchpad —\nno on-chip limit, so a true 512×512 (code supports 1024).",
     1.2, hy+1.06, 5.3, 0.8, 12.5, MUTE, font="Calibri Light", line_spacing=1.0)
box(s, 6.95, hy+0.5, 5.4, 1.45, fill=MID, line=BOXLN)
text(s, "Foam from the Jacobian", 7.2, hy+0.66, 5.0, 0.4, 16, GOLD, bold=True, font="Calibri")
text(s, "Where a wave folds over itself, one number goes negative.\nWe paint foam there — whitecaps are physical, not faked.",
     7.2, hy+1.06, 5.0, 0.8, 12.5, MUTE, font="Calibri Light", line_spacing=1.0)
page(s, 4)

# ================================================================ SLIDE 5 — The look
s = slide(); bg(s); kicker(s, "Why it looks like water")
title(s, "One rule: Fresnel")
rule(s, 0.95, 1.78, 1.4)
# diagram: surface line, eye, two rays (head-on dark, grazing mirror)
# surface
surf_y = 4.55
sine(s, 0.95, surf_y, 5.6, 0.12, 5, color=AQUA, weight=2.4)
# head-on (down) ray + grazing ray from an eye
eye_x, eye_y = 3.7, 2.5
dot(s, eye_x, eye_y, 0.16, INK)
# head-on
ln1 = s.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(eye_x), Inches(eye_y+0.08), Inches(3.0), Inches(surf_y-0.08))
ln1.line.color.rgb = MUTE; ln1.line.width=Pt(1.6); ln1.shadow.inherit=False
text(s, "head-on\ndark, see-through", 1.45, 4.8, 2.4, 0.9, 12.5, MUTE, align=PP_ALIGN.CENTER, font="Calibri")
# grazing
ln2 = s.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(eye_x), Inches(eye_y+0.08), Inches(6.2), Inches(surf_y-0.06))
ln2.line.color.rgb = AQUA; ln2.line.width=Pt(2.2); ln2.shadow.inherit=False
text(s, "grazing\nbright mirror", 5.2, 4.8, 2.2, 0.9, 12.5, AQUA, align=PP_ALIGN.CENTER, font="Calibri")
# right column: the ingredients
rx = 7.5
text(s, "Blended by that one rule:", rx, 2.35, 5.4, 0.4, 16, INK, font="Calibri Light")
ing = ["Sky reflection bounced off each wave","Depth-tinted body — navy in dips, teal at crests",
       "Sun glint + broad shimmer","Foam and distance haze","Composited in HDR, then tonemapped like a photo"]
for i,t in enumerate(ing):
    yy = 3.0 + i*0.62
    dot(s, rx+0.07, yy+0.12, 0.12, AQUA if i%2==0 else TEAL)
    text(s, t, rx+0.35, yy-0.06, 5.2, 0.5, 14.5, INK, font="Calibri Light")
text(s, "Reflections sample the sky cubemap per-pixel — cheap, stable, and reused to light the boats.",
     0.95, 6.35, 11.6, 0.4, 13, MUTE, italic=True, font="Calibri")
page(s, 5)

# ================================================================ SLIDE 6 — Boats
s = slide(); bg(s); kicker(s, "Boats that live in the water")
title(s, "A two-way exchange")
rule(s, 0.95, 1.78, 1.4)
# two facing panels with arrows between
pw, ph, py = 5.2, 2.55, 2.5
box(s, 0.95, py, pw, ph, fill=BOXBG, line=BOXLN)
text(s, "Water  →  Boat", 1.2, py+0.22, pw-0.5, 0.5, 19, AQUA, bold=True, font="Calibri")
text(s, "Sample the wave height under 15 points on the hull.",
     1.2, py+0.95, pw-0.5, 0.5, 14.5, INK, font="Calibri Light")
text(s, "Their average → how high it floats (a soft spring).\nThe slope across the hull → pitch and roll.",
     1.2, py+1.5, pw-0.5, 0.9, 13.5, MUTE, font="Calibri Light", line_spacing=1.05)
box(s, 7.15, py, pw, ph, fill=BOXBG, line=BOXLN)
text(s, "Boat  →  Water", 7.4, py+0.22, pw-0.5, 0.5, 19, TEAL, bold=True, font="Calibri")
text(s, "A moving boat drops a ripple at its stern each frame.",
     7.4, py+0.95, pw-0.5, 0.5, 14.5, INK, font="Calibri Light")
text(s, "Fed into a second wave-equation sim (256×256);\nthose ripples spread into the wake.",
     7.4, py+1.5, pw-0.5, 0.9, 13.5, MUTE, font="Calibri Light", line_spacing=1.05)
# connecting double arrow
a1 = s.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, Inches(6.18), Inches(py+0.55), Inches(0.92), Inches(0.42))
a1.fill.solid(); a1.fill.fore_color.rgb=AQUA; a1.line.fill.background(); a1.shadow.inherit=False
a2 = s.shapes.add_shape(MSO_SHAPE.LEFT_ARROW, Inches(6.18), Inches(py+1.55), Inches(0.92), Inches(0.42))
a2.fill.solid(); a2.fill.fore_color.rgb=TEAL; a2.line.fill.background(); a2.shadow.inherit=False
text(s, "Averaging is the trick:  a long ship spans many waves and rides smooth — the jet-ski feels every bump.",
     0.95, 5.5, 11.6, 0.6, 16, INK, italic=True, font="Calibri Light")
text(s, "The FFT is the open-ocean swell;  the ripple sim adds local effects it can't make — wakes and splashes — on top.",
     0.95, 6.25, 11.6, 0.5, 13, MUTE, font="Calibri")
page(s, 6)

# ================================================================ SLIDE 7 — Performance
s = slide(); bg(s); kicker(s, "Making it real-time")
title(s, "Three decisions bought real-time")
rule(s, 0.95, 1.78, 1.4)
cards = [
    ("Stockham FFT", "Goes big without the scratchpad limit — a true 512×512 surface.", AQUA),
    ("No-stall readback", "Boats read last frame's height, ready instantly. Naive 'read now' dropped us to ~55 FPS.", TEAL),
    ("Rain on the GPU", "120,000 drops: one compute update, two draw calls — almost no CPU cost.", GOLD),
]
cw, gap, x0, y0, ch = 3.78, 0.28, 0.95, 2.35, 2.7
for i,(h,sub,c) in enumerate(cards):
    x = x0 + i*(cw+gap)
    box(s, x, y0, cw, ch, fill=BOXBG, line=BOXLN)
    box(s, x, y0, cw, 0.12, fill=c, line=None)
    text(s, f"{i+1}", x+0.28, y0+0.32, 1, 0.6, 26, c, bold=True, font="Calibri Light")
    text(s, h, x+0.28, y0+1.0, cw-0.56, 0.5, 18, INK, font="Calibri")
    text(s, sub, x+0.28, y0+1.55, cw-0.56, 1.0, 14, MUTE, font="Calibri Light", line_spacing=1.1)
# big number banner
box(s, 0.95, 5.5, 11.43, 1.0, fill=MID, line=BOXLN)
text(s, "~112 FPS", 1.35, 5.62, 3.0, 0.8, 34, AQUA, bold=True, font="Calibri Light", anchor=MSO_ANCHOR.MIDDLE)
text(s, "on an RTX 3090 at 1024×768  ·  physics fixed at 1/60 s  ·  the heavy work stays on the GPU",
     4.55, 5.5, 7.6, 1.0, 15, INK, font="Calibri Light", anchor=MSO_ANCHOR.MIDDLE)
page(s, 7)

# ================================================================ SLIDE 8 — Demo / close
s = slide(); bg(s, DEEP2, DEEP, 90)
sine(s, 0.0, 5.7, SW, 0.18, 2.6, color=RGBColor(0x1E,0x55,0x7A), weight=2.0, phase=0.4)
sine(s, 0.0, 6.15, SW, 0.26, 2.0, color=RGBColor(0x2A,0x6E,0x9C), weight=2.3, phase=1.1)
sine(s, 0.0, 6.6, SW, 0.34, 1.5, color=AQUA, weight=3.0, phase=0.0)
text(s, "Demo", 0.92, 2.5, 8, 1.4, 60, INK, font="Calibri Light")
rule(s, 1.0, 3.85, 2.1, AQUA, 2.5)
text(s, "Drive the boat.  Turn calm into storm.  Sweep the sun.  Make it rain.",
     0.95, 4.1, 11.5, 0.7, 21, AQUA, font="Calibri Light")
text(s, "Same FFT throughout — I'm only changing the wave recipe.",
     0.95, 4.9, 11, 0.5, 15, MUTE, italic=True, font="Calibri")

prs.save("/home/bora/Projects/learning/RealWaterSimulatorOpenGL/RealWaterSimulator.pptx")
print("saved RealWaterSimulator.pptx with", len(prs.slides._sldIdLst), "slides")
