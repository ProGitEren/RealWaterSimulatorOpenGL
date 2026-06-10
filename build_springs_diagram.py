#!/usr/bin/env python3
"""Minimalist diagram: the three buoyancy springs (heave, pitch, roll)."""
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon, FancyArrowPatch

# theme
NAVY="#0B223C"; INK="#EAF2FA"; MUTE="#8BA3BC"
AQUA="#57C7E8"; TEAL="#2AC0AE"; GOLD="#F3C56A"; FAINT="#2C5275"
plt.rcParams["font.family"]="DejaVu Sans"

def rot(pts, ang):
    a=np.radians(ang); c,s=np.cos(a),np.sin(a)
    return [(x*c - y*s, x*s + y*c) for x,y in pts]

def place(ax, pts, cx, cy, sc, ang, ec, fc):
    p=[(x*sc, y*sc) for x,y in pts]
    p=rot(p, ang)
    p=[(x+cx, y+cy) for x,y in p]
    ax.add_patch(Polygon(p, closed=True, facecolor=fc, edgecolor=ec, lw=2.2, joinstyle="round"))

SIDE=[(-0.85,-0.05),(-0.55,-0.22),(-0.10,-0.30),(0.45,-0.26),(0.85,-0.12),
      (1.05,0.10),(0.95,0.30),(-0.78,0.30)]
FRONT=[(-0.55,0.30),(-0.50,-0.02),(-0.32,-0.24),(0.0,-0.30),
       (0.32,-0.24),(0.50,-0.02),(0.55,0.30)]

def coil(ax, p0, p1, turns=6, amp=0.10, color=GOLD, lw=2.2):
    p0=np.array(p0,float); p1=np.array(p1,float)
    v=p1-p0; L=np.hypot(*v); u=v/L; perp=np.array([-u[1],u[0]])
    t=np.linspace(0,1,240); lead=0.16
    off=np.where((t<lead)|(t>1-lead), 0.0,
                 amp*np.sin(2*np.pi*turns*(t-lead)/(1-2*lead)))
    pts=p0[None,:]+np.outer(t,v)+np.outer(off,perp)
    ax.plot(pts[:,0],pts[:,1],color=color,lw=lw,solid_capstyle="round")

def spiral(ax, cx, cy, r=0.16, color=AQUA, lw=2.2):
    th=np.linspace(0, 2.6*2*np.pi, 200); rr=r*th/th.max()
    ax.plot(cx+rr*np.cos(th), cy+rr*np.sin(th), color=color, lw=lw, solid_capstyle="round")

def darr(ax, a, b, rad=0.0, color=GOLD, lw=2.4, ms=16):
    cs="arc3,rad=%.2f"%rad
    ax.add_patch(FancyArrowPatch(a, b, connectionstyle=cs, arrowstyle="<|-|>",
                                 mutation_scale=ms, color=color, lw=lw))

fig, axes = plt.subplots(1,3, figsize=(12.6,4.7), dpi=200)
fig.patch.set_facecolor(NAVY)
for ax in axes:
    ax.set_xlim(-1.65,1.65); ax.set_ylim(-1.25,1.35)
    ax.set_aspect("equal"); ax.axis("off"); ax.set_facecolor(NAVY)

# thin dividers
for xfig in (0.365, 0.635):
    fig.add_artist(plt.Line2D([xfig,xfig],[0.12,0.80], color=FAINT, lw=1.0,
                              transform=fig.transFigure))

def header(ax, letter, color, caption):
    ax.text(0,1.27, letter, ha="center", va="center", fontsize=19, fontweight="bold", color=color)
    ax.text(0,-0.98, caption, ha="center", va="center", fontsize=10.5, color=MUTE)

# ---- HEAVE (gold) : straight water line, vertical spring
ax=axes[0]
ax.plot([-1.45,1.45],[-0.62,-0.62], color=MUTE, lw=1.3, ls=(0,(6,4)))
ax.text(1.42,-0.50,"average height", ha="right", va="bottom", fontsize=8.5, color=MUTE, style="italic")
place(ax, SIDE, 0,0.42, 0.95, 0, INK, "#13314F")
coil(ax, (0,0.10), (0,-0.62), turns=6, amp=0.11, color=GOLD)
darr(ax, (-0.95,0.10), (-0.95,-0.62), rad=0, color=GOLD, lw=2.4)
header(ax, "HEAVE", GOLD, "rides up and down to the\naverage water height")

# ---- PITCH (aqua) : sloped fore-aft line, boat pitched, rocking arc
ax=axes[1]
sl=14
xs=np.array([-1.45,1.45]); ys=-0.30 + np.tan(np.radians(sl))*xs
ax.plot(xs,ys, color=MUTE, lw=1.3, ls=(0,(6,4)))
ax.text(1.42, ys[1]+0.12,"fore–aft slope", ha="right", va="bottom", fontsize=8.5, color=MUTE, style="italic")
place(ax, SIDE, 0,0.30, 0.95, sl, INK, "#13314F")
darr(ax, (-0.95,0.70), (0.95,0.94), rad=-0.38, color=AQUA, lw=2.4)
spiral(ax, 0,0.18, r=0.15, color=AQUA)
header(ax, "PITCH", AQUA, "tilts nose up / down to the\nfore–aft slope")

# ---- ROLL (teal) : front view, rolled, rocking arc
ax=axes[2]
rl=15
xs=np.array([-1.35,1.35]); ys=-0.30 + np.tan(np.radians(rl))*xs
ax.plot(xs,ys, color=MUTE, lw=1.3, ls=(0,(6,4)))
ax.text(1.32, ys[1]+0.12,"cross slope", ha="right", va="bottom", fontsize=8.5, color=MUTE, style="italic")
place(ax, FRONT, 0,0.30, 1.0, rl, INK, "#13314F")
darr(ax, (-0.85,0.72), (0.85,0.96), rad=-0.38, color=TEAL, lw=2.4)
spiral(ax, 0,0.16, r=0.15, color=TEAL)
header(ax, "ROLL", TEAL, "leans side to side to the\ncross slope")

fig.text(0.5,0.93,"Boat buoyancy — three springs", ha="center", va="center",
         fontsize=16, color=INK)
fig.text(0.5,0.055,
         "The hull samples the wave height under 15 points; each spring is critically damped, so the boat never overshoots or sinks.",
         ha="center", va="center", fontsize=9.5, color=MUTE)

plt.subplots_adjust(left=0.01,right=0.99,top=0.86,bottom=0.13,wspace=0.05)
fig.savefig("/home/bora/Projects/learning/RealWaterSimulatorOpenGL/buoyancy_springs.png",
            facecolor=NAVY, dpi=200)
print("saved buoyancy_springs.png")
