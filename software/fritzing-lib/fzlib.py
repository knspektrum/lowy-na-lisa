"""Small helpers for writing a Fritzing (.fz/.fzz) sketch by hand.

Fritzing's scene is 90 px per inch. Each part's breadboard SVG gives the
local connector positions; a part instance has a top-left position plus an
optional rotation about its centre, which is how Fritzing itself stores it.
"""
import math
import re
from xml.sax.saxutils import escape, quoteattr

from svgelements import SVG

DPI = 90.0
SVG_PPI = 96.0  # svgelements renders physical units at this ppi


def svg_size_px(path):
    """Width/height of an SVG in Fritzing scene px."""
    t = open(path, encoding="utf-8").read()
    root = re.search(r"<svg\b[^>]*>", t, re.S).group(0)

    def inch(attr):
        v = re.search(r"\s" + attr + r'="\s*([0-9.]+)\s*(in|mm|cm|px|pt)?\s*"', root)
        n = float(v.group(1))
        u = v.group(2) or "px"
        # Fritzing reads bare px as 72 dpi in Illustrator exports, 90 dpi otherwise.
        px = 1 / 72 if "Adobe Illustrator" in t[:2000] else 1 / 90
        return n * {"in": 1, "mm": 1 / 25.4, "cm": 1 / 2.54, "px": px, "pt": 1 / 72}[u]

    return inch("width") * DPI, inch("height") * DPI


def svg_points(path):
    """{svg element id: (x, y)} in local scene px for every connector-ish element.

    A leg (a line drawn from the body outwards) gives its far end point;
    anything else gives the centre of its bounding box.
    """
    svg = SVG.parse(path, reify=True, ppi=SVG_PPI)
    # svgelements' user px -> Fritzing scene px (90/in); also right for SVGs sized in bare px.
    k = svg_size_px(path)[0] / svg.width
    out = {}
    for e in svg.elements():
        i = e.values.get("id") if hasattr(e, "values") else None
        if not i or not re.match(r"connector\d+(pin|leg|terminal)$", i):
            continue
        if i.endswith("leg"):
            # The attach point is the end of the leg farther from the body.
            segs = list(e.segments())
            a, b = segs[0].start if segs[0].start is not None else segs[0].end, segs[-1].end
            c = complex(svg.width / 2, svg.height / 2)
            end = max((a, b), key=lambda q: abs(complex(q.x, q.y) - c))
            out[i] = (end.x * k, end.y * k)
        else:
            bb = e.bbox(with_stroke=False)
            out[i] = ((bb[0] + bb[2]) / 2 * k, (bb[1] + bb[3]) / 2 * k)
    return out


def svg_connectors(svg_path, fzp_path):
    """{fzp connector id: attach point} using the part's breadboard svgId/legId
    mapping (a bendable leg attaches at its end, a pin at its centre)."""
    pts = svg_points(svg_path)
    t = open(fzp_path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r'<connector\b[^>]*\bid="(connector\d+)".*?</connector>', t, re.S):
        bb = re.search(r"<breadboardView>(.*?)</breadboardView>", m.group(0), re.S).group(1)
        leg = re.search(r'legId="([^"]+)"', bb)
        term = re.search(r'terminalId="([^"]+)"', bb)
        svg_id = re.search(r'svgId="([^"]+)"', bb).group(1)
        if leg:
            out[m.group(1)] = pts[leg.group(1)]
        elif term and term.group(1) in pts:
            out[m.group(1)] = pts[term.group(1)]
        else:
            out[m.group(1)] = pts[svg_id]
    return out


def rot_matrix(angle, w, h, mirror_y=False):
    """Qt-style (m11, m12, m21, m22, m31, m32): rotation by `angle` (then an
    optional top/bottom mirror) about the item centre, as Fritzing stores it."""
    a = math.radians(angle)
    c, s = round(math.cos(a), 9), round(math.sin(a), 9)
    # row-vector form: p' = p * [[m11, m12], [m21, m22]] + (m31, m32)
    m11, m12, m21, m22 = c, s, -s, c
    if mirror_y:
        m12, m22 = -m12, -m22
    cx, cy = w / 2, h / 2
    m31 = cx - (cx * m11 + cy * m21)
    m32 = cy - (cx * m12 + cy * m22)
    return (m11, m12, m21, m22, m31, m32)


def apply(mat, p):
    m11, m12, m21, m22, m31, m32 = mat
    x, y = p
    return (x * m11 + y * m21 + m31, x * m12 + y * m22 + m32)


class Sketch:
    def __init__(self):
        self.instances = []
        self.next_index = 5000
        self.z_wire = 3.5

    def idx(self):
        self.next_index += 1
        return self.next_index


class Part:
    def __init__(self, sketch, module_id, path, title, bb_svg, pos, angle=0, props=None,
                 schem_pos=(0, 0), pcb_pos=(0, 0), size=None, conns=None, fzp=None, mirror_y=False):
        self.sk = sketch
        self.index = sketch.idx()
        self.module_id = module_id
        self.path = path
        self.title = title
        self.pos = pos
        self.angle = angle
        self.props = props or {}
        self.schem_pos = schem_pos
        self.pcb_pos = pcb_pos
        if bb_svg:
            self.w, self.h = svg_size_px(bb_svg)
            self.local = svg_connectors(bb_svg, fzp or path)
        else:
            self.w, self.h = size
            self.local = conns
        self.mirror_y = mirror_y
        self.mat = rot_matrix(angle, self.w, self.h, mirror_y)
        self.links = {}  # connectorId -> [(other_conn, other_index, layer)]
        self.label_at = None  # scene position of the visible part label, if any
        self.label_size = 5
        sketch.instances.append(self)

    def place(self, conn, target):
        """Move the part so connector `conn` lands exactly on `target`."""
        x, y = apply(self.mat, self.local[conn])
        self.pos = (target[0] - x, target[1] - y)

    def bbox(self):
        cs = [apply(self.mat, p) for p in ((0, 0), (self.w, 0), (0, self.h), (self.w, self.h))]
        xs, ys = [c[0] for c in cs], [c[1] for c in cs]
        return (self.pos[0] + min(xs), self.pos[1] + min(ys), self.pos[0] + max(xs), self.pos[1] + max(ys))

    def pt(self, conn):
        x, y = apply(self.mat, self.local[conn])
        return (self.pos[0] + x, self.pos[1] + y)

    def link(self, conn, other_conn, other_index, layer):
        self.links.setdefault(conn, []).append((other_conn, other_index, layer))

    def xml(self):
        o = []
        o.append(f'<instance moduleIdRef={quoteattr(self.module_id)} modelIndex="{self.index}" path={quoteattr(self.path)}>')
        for k, v in self.props.items():
            o.append(f"  <property name={quoteattr(k)} value={quoteattr(v)}/>")
        o.append(f"  <title>{escape(self.title)}</title>")
        o.append("  <views>")
        o.append('    <breadboardView layer="breadboard">')
        tr = ""
        if self.angle % 360 or self.mirror_y:
            m11, m12, m21, m22, m31, m32 = self.mat
            tr = (f'<transform m11="{m11:g}" m12="{m12:g}" m13="0" m21="{m21:g}" m22="{m22:g}" m23="0" '
                  f'm31="{m31:.4f}" m32="{m32:.4f}" m33="1"/>')
        o.append(f'      <geometry z="2.5" x="{self.pos[0]:.4f}" y="{self.pos[1]:.4f}">{tr}</geometry>')
        if self.label_at:
            lx, ly = self.label_at
            o.append(f'      <titleGeometry visible="true" x="{lx:.3f}" y="{ly:.3f}" z="13" xOffset="0" yOffset="0" '
                     f'textColor="#000000" fontSize="{self.label_size}"><displayKey key=""/></titleGeometry>')
        if self.links:
            o.append("      <connectors>")
            for c, lst in self.links.items():
                o.append(f'        <connector connectorId="{c}" layer="breadboard"><geometry x="0" y="0"/><connects>')
                for oc, oi, layer in lst:
                    o.append(f'          <connect connectorId="{oc}" modelIndex="{oi}" layer="{layer}"/>')
                o.append("        </connects></connector>")
            o.append("      </connectors>")
        o.append("    </breadboardView>")
        o.append('    <schematicView layer="schematic">')
        o.append(f'      <geometry z="2.5" x="{self.schem_pos[0]}" y="{self.schem_pos[1]}"/>')
        o.append("    </schematicView>")
        o.append('    <pcbView layer="copper0">')
        o.append(f'      <geometry z="2.5" x="{self.pcb_pos[0]}" y="{self.pcb_pos[1]}"/>')
        o.append("    </pcbView>")
        o.append("  </views>")
        o.append("</instance>")
        return "\n".join(o)


class Wire:
    MILS = "22.2222"

    def __init__(self, sketch, p0, p1, color, mils=None):
        self.sk = sketch
        self.mils = mils or self.MILS
        self.index = sketch.idx()
        self.p0, self.p1 = p0, p1
        self.color = color
        self.links = {"connector0": [], "connector1": []}
        sketch.z_wire += 0.0001
        self.z = sketch.z_wire
        sketch.instances.append(self)

    def xml(self):
        x, y = self.p0
        dx, dy = self.p1[0] - x, self.p1[1] - y
        o = [f'<instance moduleIdRef="WireModuleID" modelIndex="{self.index}" path=":/resources/parts/core/wire.fzp">',
             f"  <title>Wire{self.index}</title>",
             "  <views>",
             '    <breadboardView layer="breadboardWire">',
             f'      <geometry z="{self.z:.5f}" x="{x:.4f}" y="{y:.4f}" x1="0" y1="0" x2="{dx:.4f}" y2="{dy:.4f}" wireFlags="64"/>',
             f'      <wireExtras mils="{self.mils}" color="{self.color}" opacity="1" banded="0"/>',
             "      <connectors>"]
        for c in ("connector0", "connector1"):
            o.append(f'        <connector connectorId="{c}" layer="breadboardWire"><geometry x="0" y="0"/><connects>')
            for oc, oi, layer in self.links[c]:
                o.append(f'          <connect connectorId="{oc}" modelIndex="{oi}" layer="{layer}"/>')
            o.append("        </connects></connector>")
        o += ["      </connectors>", "    </breadboardView>", "  </views>", "</instance>"]
        return "\n".join(o)


class Note:
    """A Fritzing breadboard note (rich-text box)."""

    def __init__(self, sketch, pos, size, html_body):
        self.index = sketch.idx()
        self.pos, self.size, self.body = pos, size, html_body
        sketch.instances.append(self)

    def xml(self):
        html = ('<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0//EN" "http://www.w3.org/TR/REC-html40/strict.dtd">'
                '<html><head><meta name="qrichtext" content="1" /><style type="text/css">p, li { white-space: pre-wrap; }'
                "</style></head><body style=\" font-family:'Droid Sans'; font-size:7pt;\">" + self.body + "</body></html>")
        return "\n".join([
            f'<instance moduleIdRef="NoteModuleID" modelIndex="{self.index}" path=":/resources/parts/core/note.fzp">',
            f"  <title>Note{self.index}</title>",
            f"  <text>{escape(html)}</text>",
            "  <views>",
            '    <breadboardView layer="breadboardNote">',
            f'      <geometry z="6.5" x="{self.pos[0]:.3f}" y="{self.pos[1]:.3f}" width="{self.size[0]}" height="{self.size[1]}"/>',
            "    </breadboardView>",
            "  </views>",
            "</instance>"])


def net(sketch, nodes, edges):
    """Draw a wire graph. nodes: name -> (x, y) or (Part, connectorId);
    edges: [(node_a, node_b, color)]. Every wire end meeting at a node is
    connected to every other one there (Fritzing bendpoint/junction), and to
    the part connector when the node is one."""
    def where(n):
        v = nodes[n]
        return v[0].pt(v[1]) if isinstance(v[0], Part) else v

    ends = {}
    for a, b, color in edges:
        w = Wire(sketch, where(a), where(b), color)
        ends.setdefault(a, []).append((w, "connector0"))
        ends.setdefault(b, []).append((w, "connector1"))
    for n, lst in ends.items():
        v = nodes[n]
        for w, c in lst:
            for w2, c2 in lst:
                if w2 is not w:
                    w.links[c].append((c2, w2.index, "breadboardWire"))
            if isinstance(v[0], Part):
                part, pc = v
                w.links[c].append((pc, part.index, "breadboardbreadboard"))
                part.link(pc, c, w.index, "breadboardWire")


def route(sketch, a, a_conn, b, b_conn, via, color):
    """Chain of straight wires from part a's connector through the via points
    to part b's connector, connected end to end the way Fritzing stores bendpoints."""
    pts = [a.pt(a_conn)] + list(via) + [b.pt(b_conn)]
    wires = [Wire(sketch, pts[i], pts[i + 1], color) for i in range(len(pts) - 1)]
    w0, wn = wires[0], wires[-1]
    w0.links["connector0"].append((a_conn, a.index, "breadboardbreadboard"))
    a.link(a_conn, "connector0", w0.index, "breadboardWire")
    wn.links["connector1"].append((b_conn, b.index, "breadboardbreadboard"))
    b.link(b_conn, "connector1", wn.index, "breadboardWire")
    for w1, w2 in zip(wires, wires[1:]):
        w1.links["connector1"].append(("connector0", w2.index, "breadboardWire"))
        w2.links["connector0"].append(("connector1", w1.index, "breadboardWire"))
    return wires


def write_fz(sketch, path, title):
    body = "\n".join(i.xml() for i in sketch.instances)
    doc = f"""<?xml version="1.0" encoding="UTF-8"?>
<module fritzingVersion="1.0.6" icon=".png">
  <title>{escape(title)}</title>
  <views>
    <view name="breadboardView" backgroundColor="#ffffff" gridSize="0.1in" showGrid="0" alignToGrid="0" viewFromBelow="0"/>
    <view name="schematicView" backgroundColor="#ffffff" gridSize="0.3in" showGrid="0" alignToGrid="0" viewFromBelow="0"/>
    <view name="pcbView" backgroundColor="#333333" gridSize="0.05in" showGrid="0" alignToGrid="0" viewFromBelow="0"/>
  </views>
  <instances>
{body}
  </instances>
</module>
"""
    open(path, "w", encoding="utf-8").write(doc)


def check_crossings(sketch, allow=lambda a, b: False):
    """Raises if two wires cross or overlap along a run (unless they are
    joined). `allow(wire_a, wire_b)` may permit a crossing (e.g. ground running
    under signal wires); overlaps are never allowed."""
    wires = [w for w in sketch.instances if isinstance(w, Wire)]
    linked = {(w.index, oi) for w in wires for lst in w.links.values() for _, oi, _ in lst}

    def seg(w):
        return w.p0, w.p1

    def inter(a, b):
        (x1, y1), (x2, y2) = a
        (x3, y3), (x4, y4) = b
        d = (x2 - x1) * (y4 - y3) - (y2 - y1) * (x4 - x3)
        if abs(d) < 1e-9:  # parallel: overlap if colinear and ranges meet
            if abs((x3 - x1) * (y2 - y1) - (y3 - y1) * (x2 - x1)) > 1e-6:
                return None
            ax = sorted([x1, x2]) if abs(x2 - x1) > abs(y2 - y1) else sorted([y1, y2])
            bx = sorted([x3, x4]) if abs(x2 - x1) > abs(y2 - y1) else sorted([y3, y4])
            return "overlap" if min(ax[1], bx[1]) - max(ax[0], bx[0]) > 1e-6 else None
        t = ((x3 - x1) * (y4 - y3) - (y3 - y1) * (x4 - x3)) / d
        u = ((x3 - x1) * (y2 - y1) - (y3 - y1) * (x2 - x1)) / d
        eps = 1e-6
        return "cross" if eps < t < 1 - eps and eps < u < 1 - eps else None

    bad = []
    for i, a in enumerate(wires):
        for b in wires[i + 1:]:
            if (a.index, b.index) in linked:
                continue
            kind = inter(seg(a), seg(b))
            if kind and (kind == "overlap" or not allow(a, b)):
                bad.append((kind, a.p0, a.p1, a.color, b.p0, b.p1, b.color))
    if bad:
        raise SystemExit("wire crossings/overlaps:\n" + "\n".join(map(str, bad)))
