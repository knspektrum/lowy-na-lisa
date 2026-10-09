"""Builds led-receiver.fzz, the Fritzing sketch of the LED receiver wiring.

    nix-shell -p 'python3.withPackages(p:[p.svgelements])' \
        --run 'python3 make_fzz.py <fritzing-parts-dir>'

where <fritzing-parts-dir> is Fritzing's parts library (e.g.
$(nix build nixpkgs#fritzing --print-out-paths)/share/fritzing/parts).
Then export it with `Fritzing -svg <dir>` (see README.md).

Parts: ESP32 38-pin "wide" DevKit (community part by Thomas Plunkett, its
silkscreen fixed to read CMD instead of GND next to V5, as on the real
board), Fritzing core 5 mm LEDs, resistors, tactile pushbutton, slide switch
and 1000 mAh LiPo, the Adafruit bq25185 + 5V boost breakout (USB-C charger
with 5 V boost) standing in for the actual USB-C charger/boost board, and a
generic SIP-4 IC standing in for the Seeed LoRa-E5.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../../fritzing-lib"))
from fzlib import Part, Sketch, check_crossings, net, write_fz  # noqa: E402

LIB = sys.argv[1]
BB = os.path.join(LIB, "svg/core/breadboard")
CORE = os.path.join(LIB, "core")
OWN = os.path.join(HERE, "parts")

R_RED_YELLOW = "160Ω"  # measured 157 Ω
R_GREEN = "30Ω"        # measured 30 Ω

BLUE, RED, BLACK, YELLOW, GREEN, PURPLE = "#418dd9", "#cc1414", "#404040", "#ffe24d", "#47cc79", "#ab58a2"

sk = Sketch()

# ESP32 with USB up, the way it is held when reading the pins
# "V5, CMD, SD3, SD2, G13, GND, ..." down the right-hand side.
esp = Part(sk, "ESP32-38PinWide-CMDfix", "part.ESP32-38PinWide.fzp", "ESP32 DevKit",
           os.path.join(OWN, "svg.breadboard.ESP32-38PinWide_breadboard.svg"), (0, 0), 180,
           fzp=os.path.join(OWN, "part.ESP32-38PinWide.fzp"), schem_pos=(0, 0), pcb_pos=(0, 0))
E = {  # fzp connector ids on the ESP32 part
    "3V3": "connector0", "G32": "connector6", "G33": "connector7", "G25": "connector8",
    "G26": "connector9", "G27": "connector10", "G14": "connector11", "G12": "connector12",
    "GND_R": "connector13", "G13": "connector14", "RX2": "connector26", "TX2": "connector27",
    "GND_L": "connector31",
}
ey = {k: esp.pt(v)[1] for k, v in E.items()}

# LED1 (weakest signal) .. LED7, as LED_PINS[] in led-receiver.ino.
LEDS = [
    ("G33", "Red (633nm)", R_RED_YELLOW),
    ("G25", "Red (633nm)", R_RED_YELLOW),
    ("G26", "Yellow (585nm)", R_RED_YELLOW),
    ("G27", "Yellow (585nm)", R_RED_YELLOW),
    ("G14", "Yellow (585nm)", R_RED_YELLOW),
    ("G12", "Green (555nm)", R_GREEN),
    ("G13", "Green (555nm)", R_GREEN),
]

# The LEDs form a column right of the ESP32, domes pointing right, LED7 at the
# top and LED1 at the bottom, one row above the pins so the button fits under
# the column. Each row: GPIO -> resistor -> anode (upper leg); cathode (lower
# leg) -> ground bus that runs vertically underneath the signal wires.
# Fan-out: rows above their pin bend upwards (upper wire bends first), rows
# below bend downwards (lower wire bends first).
ROW = 24.0
X_TURN0, X_TURN_STEP = 109.0, 7.0
X_GBUS = 160.0
X_RES = 172.0
X_LEG = 222.0
X_BTN = 102.0

g_nodes, g_edges = {}, []  # ground net, drawn first so it sits under the signals
s_nodes, s_edges = {}, []
a7 = ey["G13"] - ROW
rows = {i + 1: a7 + (6 - i) * ROW for i in range(7)}
up = sorted([n for n in rows if rows[n] < ey[LEDS[n - 1][0]] - 0.5], key=lambda n: rows[n])
down = sorted([n for n in rows if rows[n] > ey[LEDS[n - 1][0]] + 0.5], key=lambda n: -rows[n])
x_turn = {n: X_TURN0 + X_TURN_STEP * k for group in (up, down) for k, n in enumerate(group)}
bus = []  # (y, node) along the ground bus
for i, (pin, color, ohms) in enumerate(LEDS):
    n = i + 1
    ya = rows[n]
    r = Part(sk, "ResistorModuleID", "core/resistor.fzp", f"R{n}", os.path.join(BB, "resistor_220.svg"), (0, 0), 0,
             {"resistance": ohms, "tolerance": "±5%", "pin spacing": "400 mil"},
             fzp=os.path.join(CORE, "resistor.fzp"), schem_pos=(250 + 40 * i, 0), pcb_pos=(250 + 40 * i, 0))
    r.place("connector0", (X_RES, ya))
    led = Part(sk, "5mmColorLEDModuleID", "core/LED-generic-5mm.fzp", f"LED{n}",
               os.path.join(BB, "LED-5mm-red-leg.svg"), (0, 0), 90, {"color": color},
               fzp=os.path.join(CORE, "LED-generic-5mm.fzp"), schem_pos=(250 + 40 * i, 120),
               pcb_pos=(250 + 40 * i, 120), mirror_y=True)
    led.place("connector1", (X_LEG, ya))  # anode, upper leg
    yk = led.pt("connector0")[1]           # cathode, lower leg
    assert abs(led.pt("connector0")[0] - X_LEG) < 1e-6 and abs(yk - ya - 9) < 0.05, led.pt("connector0")
    led.label_at = (led.bbox()[2] + 3, ya)

    px, py = esp.pt(E[pin])
    s_nodes.update({f"p{n}": (esp, E[pin]), f"r{n}a": (r, "connector0"), f"r{n}b": (r, "connector1"),
                    f"a{n}": (led, "connector1")})
    if n not in x_turn:
        s_edges.append((f"p{n}", f"r{n}a", BLUE))
    else:
        s_nodes.update({f"t{n}a": (x_turn[n], py), f"t{n}b": (x_turn[n], ya)})
        s_edges += [(f"p{n}", f"t{n}a", BLUE), (f"t{n}a", f"t{n}b", BLUE), (f"t{n}b", f"r{n}a", BLUE)]
    s_edges.append((f"r{n}b", f"a{n}", BLUE))
    g_nodes.update({f"k{n}": (led, "connector0"), f"gb{n}": (X_GBUS, yk)})
    g_edges.append((f"k{n}", f"gb{n}", BLACK))
    bus.append((yk, f"gb{n}"))

# GND pin between G13 and G12 joins the bus (sharing a node if a cathode row
# sits at the same height).
same = [name for yb, name in bus if abs(yb - ey["GND_R"]) < 0.05]
g_nodes["gnd"] = (esp, E["GND_R"])
if same:
    g_edges.append(("gnd", same[0], BLACK))
else:
    g_nodes["gbP"] = (X_GBUS, ey["GND_R"])
    g_edges.append(("gnd", "gbP", BLACK))
    bus.append((ey["GND_R"], "gbP"))

# Button G32 -> GND under the LED column, diagonal legs of the tactile switch
# (switched pair); its ground side joins LED1's cathode wire.
btn = Part(sk, "20A9BBEE34_ST", "core/pushbutton_4_horizontal.fzp", "SW1", os.path.join(BB, "basic_pbutton.svg"),
           (0, 0), 0, fzp=os.path.join(CORE, "pushbutton_4_horizontal.fzp"), schem_pos=(250, 240), pcb_pos=(250, 240))
Y_BTN = rows[1] + 30
btn.place("connector2", (X_RES + 6, Y_BTN))
btn.label_at = (btn.bbox()[0] - 2, btn.bbox()[3] + 2)
bo = btn.pt("connector1")
k1y = g_nodes["gb1"][1]
X_BTN_GND = bo[0] + 10
s_nodes.update({"g32": (esp, E["G32"]), "g32a": (X_BTN, ey["G32"]), "g32b": (X_BTN, Y_BTN), "btn_in": (btn, "connector2")})
s_edges += [("g32", "g32a", PURPLE), ("g32a", "g32b", PURPLE), ("g32b", "btn_in", PURPLE)]
# split LED1's cathode wire where the button's ground comes up into it
g_edges.remove(("k1", "gb1", BLACK))
g_nodes.update({"k1j": (X_BTN_GND, k1y), "btn_out": (btn, "connector1"), "bo1": (X_BTN_GND, bo[1])})
g_edges += [("k1", "k1j", BLACK), ("k1j", "gb1", BLACK), ("btn_out", "bo1", BLACK), ("bo1", "k1j", BLACK)]

# LoRa-E5, as a generic 4-pin SIP: pin1 VCC, pin2 GND, pin3 RX (PB7, pad 9),
# pin4 TX (PB6, pad 10). Rotated so its pins face the ESP32. 3V3 runs right
# under the bottom edge of the board.
sip_conns = {f"connector{i}": (4.5 + 9.0 * i, 25.65) for i in range(4)}
lora = Part(sk, "generic_sip_4_300mil", "", "LoRa-E5", None, (0, 0), 270, {"chip label": "LoRa-E5"},
            size=(36.0, 27.0), conns=sip_conns, schem_pos=(-200, 0), pcb_pos=(-200, 0))
lora.place("connector3", (-45, ey["RX2"]))
X_LGND, X_LVCC = -20.0, -32.0
Y_3V3 = esp.bbox()[3] + 6
v33 = esp.pt(E["3V3"])
s_nodes.update({
    "tx2": (esp, E["TX2"]), "rx2": (esp, E["RX2"]), "v33": (esp, E["3V3"]),
    "l_tx": (lora, "connector3"), "l_rx": (lora, "connector2"), "l_vcc": (lora, "connector0"),
    "lv1": (X_LVCC, lora.pt("connector0")[1]), "lv2": (X_LVCC, Y_3V3), "lv3": (v33[0], Y_3V3),
})
s_edges += [
    ("l_tx", "rx2", GREEN),   # LoRa-E5 TX (PB6) -> ESP32 RX2 (GPIO16)
    ("tx2", "l_rx", YELLOW),  # ESP32 TX2 (GPIO17) -> LoRa-E5 RX (PB7)
    ("l_vcc", "lv1", RED), ("lv1", "lv2", RED), ("lv2", "lv3", RED), ("lv3", "v33", RED),
]
g_nodes.update({"gnd_l": (esp, E["GND_L"]), "l_gnd": (lora, "connector1"),
                "lg1": (X_LGND, lora.pt("connector1")[1]), "lg2": (X_LGND, ey["GND_L"])})
g_edges += [("l_gnd", "lg1", BLACK), ("lg1", "lg2", BLACK), ("lg2", "gnd_l", BLACK)]

# Power, left of the ESP32: LiPo cell -> USB-C charger + 5 V boost board
# (Fritzing's Adafruit bq25185 + 5V boost breakout: battery into its JST, 5 V
# out of its terminal block) -> slide switch on the + side -> over the top of
# the board to V5. The charger's ground goes over the top to the LED ground bus.
CHG = "Adafruit-_7e4e9c15920efa1ec265cff2ed46fc39_1_BB"
chg = Part(sk, CHG, f"contrib/{CHG}.fzp", "Charger",
           os.path.join(LIB, f"svg/contrib/breadboard/{CHG}_breadboard.svg"), (0, 0), 0,
           fzp=os.path.join(LIB, f"contrib/{CHG}.fzp"), schem_pos=(-200, 200), pcb_pos=(-200, 200))
C_BAT, C_BGND, C_OUT, C_OGND = "connector85", "connector84", "connector89", "connector88"
chg.place(C_OUT, (-60.0, -85.0))
bat = Part(sk, "SparkFun-Electromechanical-LIPO-OUTLINE-1100", "core/sparkfun-electromechanical-lipo-outline-1100.fzp",
           "Battery", os.path.join(BB, "sparkfun-electromechanical_lipo-1100_breadboard.svg"), (0, 0), 0,
           fzp=os.path.join(CORE, "sparkfun-electromechanical-lipo-outline-1100.fzp"),
           schem_pos=(-300, 200), pcb_pos=(-300, 200))
bat.place("connector0", (chg.bbox()[0] - 14, chg.pt(C_BAT)[1] - 23))
sw = Part(sk, "1238DBDC00-toggle-switch", "core/basic-toggle-switch.fzp", "Power",
          os.path.join(BB, "basic_toggle_switch.svg"), (0, 0), 180,
          fzp=os.path.join(CORE, "basic-toggle-switch.fzp"), schem_pos=(-100, 200), pcb_pos=(-100, 200))
sw.place("connector1", (-30.0, -70.0))  # COM, pins facing up
sw.label_at = (sw.bbox()[0] - 2, sw.bbox()[3] + 2)
S_OUT = "connector0"  # the pin right of COM
bp, bm = bat.pt("connector0"), bat.pt("connector1")
out, ognd = chg.pt(C_OUT), chg.pt(C_OGND)
so = sw.pt(S_OUT)
v5 = esp.pt("connector18")
Y_5V_TOP, Y_GND_TOP, X_GND_UP, X_5V_DOWN = -120.0, -165.0, -50.0, 105.0
s_nodes.update({
    "bat_p": (bat, "connector0"), "c_bat": (chg, C_BAT), "bp1": (chg.pt(C_BAT)[0], bp[1]),
    "c_out": (chg, C_OUT), "s_com": (sw, "connector1"), "s_out": (sw, S_OUT), "v5": (esp, "connector18"),
    "q1": (sw.pt("connector1")[0], out[1]),
    "o1": (so[0], Y_5V_TOP), "o2": (X_5V_DOWN, Y_5V_TOP), "o3": (X_5V_DOWN, v5[1]),
})
s_edges += [
    ("bat_p", "bp1", RED), ("bp1", "c_bat", RED),
    ("c_out", "q1", RED), ("q1", "s_com", RED),
    ("s_out", "o1", RED), ("o1", "o2", RED), ("o2", "o3", RED), ("o3", "v5", RED),
]
g_nodes.update({
    "bat_m": (bat, "connector1"), "c_bgnd": (chg, C_BGND), "bm1": (chg.pt(C_BGND)[0], bm[1]),
    "c_ognd": (chg, C_OGND), "w1": (X_GND_UP, ognd[1]), "w2": (X_GND_UP, Y_GND_TOP), "gtop": (X_GBUS, Y_GND_TOP),
})
g_edges += [
    ("bat_m", "bm1", BLACK), ("bm1", "c_bgnd", BLACK),
    ("c_ognd", "w1", BLACK), ("w1", "w2", BLACK), ("w2", "gtop", BLACK),
]
bus.append((Y_GND_TOP, "gtop"))
bus.sort()
g_edges += [(a, b, BLACK) for (_, a), (_, b) in zip(bus, bus[1:])]

net(sk, g_nodes, g_edges)
net(sk, s_nodes, s_edges)
# Ground may pass under signal wires (it is drawn below them); nothing else may cross.
check_crossings(sk, allow=lambda a, b: {a.color, b.color} == {BLACK, BLUE})

out = os.path.join(HERE, "led-receiver.fz")
write_fz(sk, out, "LED receiver")
with zipfile.ZipFile(os.path.join(HERE, "led-receiver.fzz"), "w", zipfile.ZIP_DEFLATED) as z:
    z.write(out, "led-receiver.fz")
    for f in sorted(os.listdir(OWN)):
        z.write(os.path.join(OWN, f), f)
os.remove(out)
print("wrote led-receiver.fzz")
