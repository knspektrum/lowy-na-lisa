"""Builds fox-transmitter.fzz, the Fritzing sketch of the fox (beacon) transmitter:
WeMos D1 R32 (ESP32, Uno form factor) + HopeRF HM-TRLR-D-TTL-868 LoRa module.

    nix-shell -p 'python3.withPackages(p:[p.svgelements])' \\
        --run 'python3 make_fzz.py <fritzing-parts-dir>'

Parts: "Wemos D1 R32" community part by Peter Van Epp
(https://forum.fritzing.org/t/looking-for-wemos-d1-r32-esp32-uno/14487) and a
Fritzing generic SIP-11 IC standing in for the HM-TRLR-D module, with the
"SMA Antenna Connector" community part (Peter Van Epp) as its female SMA port. Wiring as in
fox-transmitter.ino: GPIO17 (TX) -> module RXD, GPIO16 (RX) <- module TXD,
GPIO14 -> module CONFIG (LOW = AT config mode).
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../../fritzing-lib"))
from fzlib import Part, Sketch, check_crossings, net, write_fz  # noqa: E402

LIB = sys.argv[1]
OWN = os.path.join(HERE, "parts")
RED, BLACK, YELLOW, GREEN, ORANGE, GREY = "#cc1414", "#404040", "#ffe24d", "#47cc79", "#ef6100", "#999999"

sk = Sketch()
r32 = Part(sk, "Wemos-D1-R32_1", "part.Wemos-D1-R32_1.fzp", "WeMos D1 R32",
           os.path.join(OWN, "svg.breadboard.Wemos-D1-R32_1_breadboard.svg"), (0, -24.0), 0,
           fzp=os.path.join(OWN, "part.Wemos-D1-R32_1.fzp"))
B = {"IO14": "connector21", "IO16": "connector19", "IO17": "connector18", "5V": "connector4",
     "GND": "connector5", "GND_VIN": "connector6", "VIN": "connector7", "GND_TOP": "connector28"}

# HM-TRLR-D as a generic 11-pin SIP standing upright right of the board, pins
# on its left side in the module's real order (top to bottom), antenna on top.
PINS = ["STATUS", "CONFIG", "GND", "5V", "RXD", "TXD", "GND2", "A_TX", "B_RX", "SLEEP", "RESET"]
sip = Part(sk, f"generic_sip_{len(PINS)}_300mil", "", "HM-TRLR-D", None, (0, 0), 90, {"chip label": "HM-TRLR-D"},
           size=(9.0 * len(PINS), 27.0), conns={f"connector{i}": (4.5 + 9.0 * i, 25.65) for i in range(len(PINS))},
           schem_pos=(-200, 0), pcb_pos=(-200, 0))
M = {name: f"connector{i}" for i, name in enumerate(PINS)}
X_PINS, Y_TOP = 600.0, -120.0
sip.place(M["STATUS"], (X_PINS, Y_TOP))
sx0, sy0, sx1, sy1 = sip.bbox()
# Female SMA antenna port on top of the module: "SMA Antenna Connector" community
# part by Peter Van Epp (https://forum.fritzing.org/t/sma-female-connector-part/4688),
# standing on the module with its legs in it.
SMA = "prefix0000_7e5182ad3f931725a0663bc5c876ab76_1"
sma = Part(sk, SMA, f"part.{SMA}.fzp", "SMA", os.path.join(OWN, f"svg.breadboard.{SMA}_breadboard.svg"), (0, 0), 0,
           fzp=os.path.join(OWN, f"part.{SMA}.fzp"), schem_pos=(-200, 100), pcb_pos=(-200, 100))
sma.pos = ((sx0 + sx1) / 2 - sma.w / 2, sy0 - sma.h + 4)

# Battery: 5x AA in series (a 5-cell holder) right of the board, cells standing
# side by side and snaked like the holder: + at the bottom of the first cell,
# - at the top of the last. The cell art has + on its right end, so a cell
# turned 270 degrees has + at the top and one turned 90 degrees + at the bottom.
AA = "SparkFun-Electromechanical-BATTERY-AA"
AA_FZP = os.path.join(LIB, "core/sparkfun-electromechanical-battery-aa.fzp")
AA_SVG = os.path.join(LIB, "svg/core/breadboard/sparkfun-electromechanical_battery-aa_breadboard.svg")
PLUS, MINUS = "connector0", "connector1"  # right / left end of the cell art
X_CELL1, CELL_PITCH, Y_CELL_BOTTOM = 291.0, 58.0, 183.0
cells = []
for k in range(5):
    plus_down = k % 2 == 0
    c = Part(sk, AA, "core/sparkfun-electromechanical-battery-aa.fzp", f"AA{k + 1}", AA_SVG, (0, 0),
             90 if plus_down else 270, fzp=AA_FZP, schem_pos=(-300, 100 + 40 * k), pcb_pos=(-300, 100 + 40 * k))
    c.place(PLUS if plus_down else MINUS, (X_CELL1 + CELL_PITCH * k, Y_CELL_BOTTOM))  # bottom end
    cells.append(c)

# The board sits higher than the pack, so everything going to its power header
# runs in the strip under it and never below the cells. Battery + comes in from
# the bottom of the first cell into VIN, the rightmost pin, so the module's 5V
# (the leftmost pin) goes round the left of the board instead of crossing it.
# Module SLEEP and battery - come down the gap between the board and the pack.
X_5V_L = -6.0                         # 5V down the left of the board
X_BATM, X_SLEEP = 255.0, 261.0        # gap between the board and the pack
Y_BATM, Y_SLEEP, Y_5V_LOW = 170.0, 176.0, 170.0
Y_BATM_TOP = -9.0                     # battery - over the top of the cells
y = {n: sip.pt(M[n])[1] for n in PINS}
pins = {k: r32.pt(v) for k, v in B.items()}
assert r32.bbox()[3] < Y_BATM - 3 and Y_SLEEP < cells[0].pt(PLUS)[1] - 3
bat_plus, bat_minus = cells[0].pt(PLUS), cells[4].pt(MINUS)
nodes = {
    "cfg": (sip, M["CONFIG"]), "c1": (pins["IO14"][0], y["CONFIG"]), "io14": (r32, B["IO14"]),
    "rxd": (sip, M["RXD"]), "r1": (pins["IO17"][0], y["RXD"]), "io17": (r32, B["IO17"]),
    "txd": (sip, M["TXD"]), "t1": (pins["IO16"][0], y["TXD"]), "io16": (r32, B["IO16"]),
    "m5v": (sip, M["5V"]), "v1": (X_5V_L, y["5V"]), "v2": (X_5V_L, Y_5V_LOW), "v3": (pins["5V"][0], Y_5V_LOW),
    "b5v": (r32, B["5V"]),
    "mgnd": (sip, M["GND"]), "g1": (pins["GND_TOP"][0], y["GND"]), "bgndt": (r32, B["GND_TOP"]),
    "sleep": (sip, M["SLEEP"]), "s1": (X_SLEEP, y["SLEEP"]), "s2": (X_SLEEP, Y_SLEEP),
    "s3": (pins["GND"][0], Y_SLEEP), "bgnd": (r32, B["GND"]),
    "bp": (cells[0], PLUS), "bp1": (pins["VIN"][0], bat_plus[1]), "vin": (r32, B["VIN"]),
    "bm": (cells[4], MINUS), "bm1": (bat_minus[0], Y_BATM_TOP), "bm2": (X_BATM, Y_BATM_TOP),
    "bm3": (X_BATM, Y_BATM), "bm4": (pins["GND_VIN"][0], Y_BATM), "bgndv": (r32, B["GND_VIN"]),
}
edges = []
for k in range(4):  # series links between neighbouring cells (holder springs)
    nodes[f"l{k}a"] = (cells[k], MINUS)
    nodes[f"l{k}b"] = (cells[k + 1], PLUS)
    edges.append((f"l{k}a", f"l{k}b", GREY))
edges += [
    # power first so it is drawn underneath the signal wires it has to pass
    ("bm", "bm1", BLACK), ("bm1", "bm2", BLACK), ("bm2", "bm3", BLACK), ("bm3", "bm4", BLACK),
    ("bm4", "bgndv", BLACK),                                                                 # - -> GND
    ("bp", "bp1", RED), ("bp1", "vin", RED),                                                 # + -> VIN
    ("m5v", "v1", RED), ("v1", "v2", RED), ("v2", "v3", RED), ("v3", "b5v", RED),
    ("mgnd", "g1", BLACK), ("g1", "bgndt", BLACK),                             # module GND -> top GND
    ("sleep", "s1", BLACK), ("s1", "s2", BLACK), ("s2", "s3", BLACK), ("s3", "bgnd", BLACK),  # SLEEP low = awake
    ("io14", "c1", ORANGE), ("c1", "cfg", ORANGE),   # GPIO14 -> CONFIG
    ("io17", "r1", YELLOW), ("r1", "rxd", YELLOW),   # GPIO17 (TX) -> module RXD
    ("txd", "t1", GREEN), ("t1", "io16", GREEN),     # module TXD -> GPIO16 (RX)
]
wires = net(sk, nodes, edges)
# The pin orders force a few crossings: RX/TX swap sides, module GND passes
# CONFIG, 5V passes the top GND and CONFIG, battery + passes SLEEP and battery -,
# and battery - passes SLEEP (both ground). Nothing else may cross, and no two
# wires may overlap.
ALLOWED = [{YELLOW, GREEN}, {BLACK, YELLOW}, {BLACK, GREEN}, {BLACK, ORANGE}, {RED, ORANGE}, {BLACK, RED}]
GROUNDS = {wires[("bm1", "bm2")], wires[("s1", "s2")]}
check_crossings(sk, allow=lambda a, b: {a.color, b.color} in ALLOWED or {a, b} == GROUNDS)

out = os.path.join(HERE, "fox-transmitter.fz")
write_fz(sk, out, "Fox transmitter")
with zipfile.ZipFile(os.path.join(HERE, "fox-transmitter.fzz"), "w", zipfile.ZIP_DEFLATED) as z:
    z.write(out, "fox-transmitter.fz")
    for f in sorted(os.listdir(OWN)):
        z.write(os.path.join(OWN, f), f)
os.remove(out)
print("wrote fox-transmitter.fzz")
