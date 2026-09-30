#!/usr/bin/env python3
"""Generate the Nano ESP32 modular motherboard PCB.

The board is deliberately a two-copper-layer prototype.  The exported toner
PDFs are 1:1; for a home-made board the F.Cu sheet must be aligned to B.Cu.
"""
from pathlib import Path
import pcbnew

OUT = Path(__file__).resolve().parent
MM = pcbnew.FromMM
V = pcbnew.VECTOR2I

board = pcbnew.BOARD()
board.GetDesignSettings().SetCopperLayerCount(2)

nets = {}


def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


def fp(ref, value, x, y):
    f = pcbnew.FOOTPRINT(board)
    f.SetReference(ref)
    f.SetValue(value)
    f.SetPosition(V(MM(x), MM(y)))
    board.Add(f)
    return f


def pad(f, number, dx, dy, net_name="", drill=1.0, size=2.2, square=False):
    p = pcbnew.PAD(f)
    p.SetNumber(str(number))
    p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
    p.SetShape(pcbnew.PAD_SHAPE_RECT if square else pcbnew.PAD_SHAPE_CIRCLE)
    p.SetSize(V(MM(size), MM(size)))
    p.SetDrillSize(V(MM(drill), MM(drill)))
    p.SetPosition(V(f.GetPosition().x + MM(dx), f.GetPosition().y + MM(dy)))
    if net_name:
        p.SetNet(net(net_name))
    f.Add(p)
    return (p.GetPosition().x / MM(1), p.GetPosition().y / MM(1))


def text(s, x, y, size=1.2, layer=pcbnew.F_SilkS, angle=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(MM(x), MM(y)))
    t.SetTextSize(V(MM(size), MM(size)))
    t.SetTextThickness(MM(max(0.2, size / 6)))
    t.SetLayer(layer)
    t.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T))
    board.Add(t)


def line(x1, y1, x2, y2, layer, width=0.4):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(MM(x1), MM(y1)))
    s.SetEnd(V(MM(x2), MM(y2)))
    s.SetLayer(layer)
    s.SetWidth(MM(width))
    board.Add(s)


def track(points, net_name, layer=pcbnew.B_Cu, width=0.6):
    for a, b in zip(points, points[1:]):
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(V(MM(a[0]), MM(a[1])))
        tr.SetEnd(V(MM(b[0]), MM(b[1])))
        tr.SetWidth(MM(width))
        tr.SetLayer(layer)
        tr.SetNet(net(net_name))
        board.Add(tr)


def outline_rect(x1, y1, x2, y2, layer=pcbnew.F_SilkS, width=0.35):
    line(x1, y1, x2, y1, layer, width)
    line(x2, y1, x2, y2, layer, width)
    line(x2, y2, x1, y2, layer, width)
    line(x1, y2, x1, y1, layer, width)


# 205 x 138 mm, four M3 mounting holes.
outline_rect(5, 5, 210, 143, pcbnew.Edge_Cuts, 0.1)
for i, (x, y) in enumerate(((10, 10), (205, 10), (10, 138), (205, 138)), 1):
    f = fp(f"H{i}", "M3", x, y)
    pad(f, 1, 0, 0, "", drill=3.2, size=6.0)

# High-voltage section.
line(62, 5, 62, 143, pcbnew.F_SilkS, 0.8)
text("DANGER 230 VAC", 33, 9, 2.0)
text("KEEP 8 mm CLEAR", 33, 13, 1.2)
outline_rect(8, 18, 57, 76)

j1 = fp("J1", "AC_IN_5.08", 13, 30)
ac_l = pad(j1, 1, 0, 0, "AC_L", drill=1.3, size=3.2, square=True)
ac_n = pad(j1, 2, 5.08, 0, "AC_N", drill=1.3, size=3.2)
text("230V L  N", 15.5, 26.5, 1.2)

f1 = fp("F1", "FUSE_T500mA", 28, 30)
f1a = pad(f1, 1, 0, 0, "AC_L", drill=1.1, size=2.8, square=True)
f1b = pad(f1, 2, 15, 0, "AC_L_FUSED", drill=1.1, size=2.8)
text("FUSE T500mA", 35.5, 26.5, 1.1)

mov = fp("RV1", "MOV_275VAC", 47, 39)
mov1 = pad(mov, 1, 0, 0, "AC_L_FUSED", drill=1.0, size=2.6, square=True)
mov2 = pad(mov, 2, 5, 0, "AC_N", drill=1.0, size=2.6)
text("MOV 275VAC", 49.5, 35.5, 1.0)

ps = fp("PS1", "ACDC_38x23_3V3", 13, 48)
ps1 = pad(ps, 1, 0, 0, "AC_L_FUSED", drill=1.0, size=2.6, square=True)
ps2 = pad(ps, 2, 0, 8.5, "AC_N", drill=1.0, size=2.6)
ps3 = pad(ps, 3, 35.8, 20.5, "GND", drill=1.0, size=2.6)
ps4 = pad(ps, 4, 35.8, 0, "+3V3", drill=1.0, size=2.6)
outline_rect(13, 48, 51, 71)
text("38x23 mm AC/DC", 32, 73.5, 1.0)

# AC routing, generous widths and no copper beyond the isolation boundary.
track([ac_l, f1a], "AC_L", width=1.5)
track([f1b, (47, 30), mov1], "AC_L_FUSED", width=1.5)
track([mov1, (43, 39), (43, 48), ps1], "AC_L_FUSED", width=1.5)
track([ac_n, (18.08, 43), (13, 43), ps2], "AC_N", width=1.5)
track([mov2, (54, 39), (54, 56.5), ps2], "AC_N", width=1.5)

# Low-voltage bulk capacitor and 3.3 V distribution.
c1 = fp("C1", "1000uF_6V3", 69, 18)
c1p = pad(c1, 1, 0, 0, "+3V3", drill=0.9, size=2.4, square=True)
c1n = pad(c1, 2, 5, 0, "GND", drill=0.9, size=2.4)
text("1000uF >=6.3V", 74, 14.5, 1.0)

# Nano ESP32 socket, standard Nano 2x15 (2.54 pitch, 15.24 row spacing).
nano = fp("A1", "Arduino_Nano_ESP32_socket", 88, 50)
left_names = ["D1_TX", "D0_RX", "RESET", "GND", "D2", "D3", "D4", "D5",
              "D6", "D7", "D8", "D9", "D10", "D11_MOSI", "D12_MISO"]
right_names = ["D13_SCK", "+3V3", "AREF", "A0", "A1", "A2_RX", "A3_TX",
               "A4_TX", "A5_RX", "A6", "A7", "+5V_NANO", "RESET", "GND", "VIN"]
nano_pos = {}
for i, name in enumerate(left_names):
    nano_pos[name] = pad(nano, i + 1, 0, i * 2.54, name, square=(i == 0))
for i, name in enumerate(right_names):
    nano_pos[name] = pad(nano, 30 - i, 15.24, i * 2.54, name)
outline_rect(86.5, 47.5, 104.74, 88.5)
text("NANO ESP32", 95.6, 45.5, 1.4)

# AP/config button.
sw = fp("SW1", "AP_CONFIG_BUTTON", 72, 90)
sw1 = pad(sw, 1, 0, 0, "D0_RX", drill=1.0, size=2.8, square=True)
sw2 = pad(sw, 2, 6.5, 0, "GND", drill=1.0, size=2.8)
text("HOLD: AP / RESTART", 75, 95, 1.0)

# Thermistor divider.
ntc = fp("TH1", "NTC_10K", 72, 105)
ntcp = pad(ntc, 1, 0, 0, "+3V3", square=True)
ntcm = pad(ntc, 2, 5, 0, "A0")
r1 = fp("R1", "10K", 77, 113)
r1a = pad(r1, 1, 0, 0, "A0", square=True)
r1b = pad(r1, 2, 10, 0, "GND")
text("NTC 10K + R 10K", 80, 119, 1.0)

# Three local input terminals.
jin = fp("J2", "LOCAL_INPUTS_5.08", 67, 132)
for i, name in enumerate(("D4", "D5", "D6", "GND")):
    pad(jin, i + 1, i * 5.08, 0, name, drill=1.3, size=3.2, square=(i == 0))
text("IN1  IN2  IN3  GND", 74.5, 127.5, 1.0)

# LoRa RFM95W footprint: 16 x 16 mm, 2 mm pitch.
lora = fp("U2", "RFM95W_16x16_P2.0", 116, 17)
lora_left = ["GND", "D12_MISO", "D11_MOSI", "D13_SCK", "D10", "D9", "LORA_DIO5", "GND"]
lora_right = ["D2", "LORA_DIO1", "LORA_DIO0_ALT", "+3V3", "LORA_DIO4",
              "LORA_DIO3", "GND", "ANT"]
lora_pos = {}
for i, name in enumerate(lora_left):
    lora_pos[f"L{i+1}"] = pad(lora, i + 1, 0, i * 2, name, drill=0.8, size=1.8,
                              square=(i == 0))
for i, name in enumerate(lora_right):
    lora_pos[f"R{i+1}"] = pad(lora, 16 - i, 16, i * 2, name, drill=0.8, size=1.8)
outline_rect(116, 16, 132, 32)
text("RFM95W / 2mm ADAPTER", 124, 12.5, 1.0)

# WIZ850io carrier socket: two 1x6 rows, 2.54 mm.
eth = fp("U3", "WIZ850io_socket", 147, 16)
eth_a = ["GND", "D11_MOSI", "D13_SCK", "D7", "D8", "RESET"]
eth_b = ["+3V3", "D12_MISO", "ETH_NC1", "ETH_NC2", "ETH_NC3", "GND"]
eth_pos = {}
for i, name in enumerate(eth_a):
    eth_pos[f"A{i+1}"] = pad(eth, i + 1, 0, i * 2.54, name, square=(i == 0))
for i, name in enumerate(eth_b):
    eth_pos[f"B{i+1}"] = pad(eth, 12 - i, 25, i * 2.54, name)
outline_rect(147, 15, 172, 40)
text("WIZ850io SOCKET", 159.5, 12, 1.1)

# XY-485 socket.
rs = fp("U4", "XY-485_1x4", 118, 105)
rs_pos = {}
for i, name in enumerate(("GND", "A4_TX", "A5_RX", "+3V3")):
    rs_pos[name] = pad(rs, i + 1, i * 2.54, 0, name, square=(i == 0))
outline_rect(116, 101, 171, 126)
text("XY-485: GND RXD TXD VCC", 143, 129, 1.0)

# SIM7600G-H HAT footprint, standard Raspberry Pi 2x20 header.
gsm = fp("U5", "SIM7600G-H_HAT_2x20", 181, 55)
gsm_pos = {}
for row, x in enumerate((0, 2.54)):
    for col in range(20):
        physical = col * 2 + row + 1
        name = f"GSM_P{physical}"
        if physical in (2, 4):
            name = "+5V_GSM"
        elif physical in (6, 9, 14, 20, 25, 30, 34, 39):
            name = "GND"
        elif physical == 8:
            name = "A2_RX"  # HAT TXD -> Nano RX
        elif physical == 10:
            name = "A3_TX"  # HAT RXD <- Nano TX
        gsm_pos[physical] = pad(gsm, physical, x, col * 2.54, name,
                                square=(physical == 1))
outline_rect(149, 45, 205.2, 110.2)
text("SIM7600G-H HAT", 177, 42, 1.3)

jgsm = fp("J3", "GSM_5V_IN_5.08", 181, 121)
gsm5 = pad(jgsm, 1, 0, 0, "+5V_GSM", drill=1.3, size=3.2, square=True)
gsmg = pad(jgsm, 2, 5.08, 0, "GND", drill=1.3, size=3.2)
text("GSM 5V >=3A", 183.5, 116.5, 1.1)

# Low-voltage power rails.  These are intentionally wide.
track([ps4, (64, 48), (64, 18), c1p], "+3V3", width=1.5)
track([ps3, (58, 68.5), (58, 22), (74, 22), c1n], "GND", width=1.5)
track([c1p, (82, 18), (82, 52.54), nano_pos["+3V3"]], "+3V3", width=1.5)
track([c1n, (84, 18), (84, 57.62), nano_pos["GND"]], "GND", width=1.5)

# Functional signal routes. Shared SPI is routed as trunks on the back.
track([nano_pos["D13_SCK"], (108, 50), (108, 23), lora_pos["L4"]], "D13_SCK")
track([(108, 23), (141, 23), eth_pos["A3"]], "D13_SCK")
track([nano_pos["D11_MOSI"], (110, 85.56), (110, 21), lora_pos["L3"]], "D11_MOSI")
track([(110, 21), (139, 21), (139, 18.54), eth_pos["A2"]], "D11_MOSI")
track([nano_pos["D12_MISO"], (112, 85.56), (112, 19), lora_pos["L2"]], "D12_MISO")
track([(112, 19), (176, 19), (176, 18.54), eth_pos["B2"]], "D12_MISO")
track([nano_pos["D10"], (106, 80.48), (106, 25), lora_pos["L5"]], "D10")
track([nano_pos["D9"], (104, 77.94), (104, 27), lora_pos["L6"]], "D9")
track([nano_pos["D2"], (86, 60.16), (86, 17), lora_pos["R1"]], "D2")
track([nano_pos["D7"], (114, 72.86), (114, 30), (140, 30), eth_pos["A4"]], "D7")
track([nano_pos["D8"], (115, 75.4), (115, 32.7), (141, 32.7), eth_pos["A5"]], "D8")

# Local functions.
track([nano_pos["D0_RX"], (80, 52.54), (80, 90), sw1], "D0_RX")
track([sw2, (82, 90), (82, 88), nano_pos["GND"]], "GND")
track([nano_pos["A0"], (107, 57.62), (107, 105), ntcm], "A0")
track([ntcm, r1a], "A0")
track([r1b, (90, 113), (90, 88), nano_pos["GND"]], "GND")
track([ntcp, (68, 105), (68, 18), c1p], "+3V3")
for idx, name in enumerate(("D4", "D5", "D6")):
    p = (67 + idx * 5.08, 132)
    track([nano_pos[name], (84 - idx * 2, nano_pos[name][1]), (84 - idx * 2, 124),
           (p[0], 124), p], name)
track([(82.24, 132), (90, 132), (90, 88), nano_pos["GND"]], "GND")

# RS485 and GSM UART.
track([nano_pos["A4_TX"], (112, 67.78), (112, 105), rs_pos["A4_TX"]], "A4_TX")
track([nano_pos["A5_RX"], (114, 70.32), (114, 108), (123.08, 108), rs_pos["A5_RX"]], "A5_RX")
track([rs_pos["+3V3"], (130, 105), (130, 95), (82, 95), (82, 18), c1p], "+3V3")
track([rs_pos["GND"], (116, 105), (116, 98), (90, 98), (90, 88), nano_pos["GND"]], "GND")
track([nano_pos["A2_RX"], (120, 62.7), (120, 60.08), gsm_pos[8]], "A2_RX")
track([nano_pos["A3_TX"], (122, 65.24), (122, 62.62), gsm_pos[10]], "A3_TX")
track([gsm5, (195, 121), (195, 57.54), gsm_pos[2]], "+5V_GSM", width=1.5)
track([gsmg, (198, 121), (198, 62.62), gsm_pos[6]], "GND", width=1.5)

# Module power spurs (some on front copper to avoid back-side crossings).
track([lora_pos["R4"], (136, 23), (136, 42), (82, 42), (82, 18), c1p],
      "+3V3", pcbnew.F_Cu, 1.0)
track([eth_pos["B1"], (176, 16), (176, 42), (82, 42), (82, 18), c1p],
      "+3V3", pcbnew.F_Cu, 1.0)
track([lora_pos["L1"], (114, 17), (114, 40), (84, 40), (84, 18), c1n],
      "GND", pcbnew.F_Cu, 1.0)
track([eth_pos["A1"], (145, 16), (145, 44), (84, 44), (84, 18), c1n],
      "GND", pcbnew.F_Cu, 1.0)

# Isolation reminders and board identity.
text("NO COPPER / 8mm ISOLATION", 61, 92, 1.0, pcbnew.F_SilkS, 90)
text("NANO ESP32 MODULAR GATEWAY REV A", 134, 139, 1.5)
text("3V3 LOGIC ONLY | GSM: SEPARATE 5V/3A", 134, 135.5, 1.0)
text("F.Cu traces require insulated jumpers or double-sided PCB", 133, 132.5, 0.9)

pcbnew.SaveBoard(str(OUT / "nano_esp32_modular_gateway.kicad_pcb"), board)
print(OUT / "nano_esp32_modular_gateway.kicad_pcb")
