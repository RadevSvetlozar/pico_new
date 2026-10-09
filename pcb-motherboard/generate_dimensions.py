#!/usr/bin/env python3
"""Read actual KiCad geometry; export dimension drawings and complete tables."""
from pathlib import Path
import re, json, math, csv, html
from collections import Counter
ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'dimensions'
OUT.mkdir(exist_ok=True)
tokens = iter(re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]', (ROOT / 'nano_esp32_modular_gateway.kicad_pcb').read_text()))
def parse():
    result=[]
    for token in tokens:
        if token == ')': return result
        result.append(parse() if token == '(' else json.loads(token) if token.startswith('"') else token)
    return result
board=parse()[0]
def children(node,key): return [n for n in node if isinstance(n,list) and n and n[0]==key]
def field(node,key,default=None):
    items=children(node,key)
    return items[0][1:] if items else default

def pair(node,key): return tuple(map(float,field(node,key)[:2]))
lines=children(board,'gr_line')
edges=[l for l in lines if field(l,'layer')==['Edge.Cuts']]
coords=[pair(l,k) for l in edges for k in ('start','end')]
x0,y0=min(x for x,y in coords),min(y for x,y in coords)
w,h=max(x for x,y in coords)-x0,max(y for x,y in coords)-y0
# These are drawn allocation outlines in the existing project, not verified bodies.
boxes={'PS1':(13,48,51,71),'A1':(86.5,47.5,104.74,88.5),'U2':(116,16,132,32),'U3':(147,15,172,40),'U4':(116,101,171,126),'U5':(149,45,205.2,110.2)}
parts=[]; pads=[]
for fp in children(board,'footprint'):
    props={p[1]:p[2] for p in children(fp,'property')}
    ref=props['Reference']; fx,fy=pair(fp,'at')
    local=[]
    for p in children(fp,'pad'):
        px,py=pair(p,'at'); sx,sy=pair(p,'size'); drill=field(p,'drill')
        row=[ref,p[1],round(fx+px-x0,4),round(fy+py-y0,4),sx,sy,float(drill[0]),field(p,'net',[''])[0]]
        pads.append(row); local.append(row)
    xs=sorted(set(p[2] for p in local)); ys=sorted(set(p[3] for p in local))
    pitch=lambda a: ', '.join(f'{step:g}' for step in sorted(set(round(b-a1,4) for a1,b in zip(a,a[1:])))) or '—'
    body=boxes.get(ref)
    parts.append([ref,props['Value'],fx-x0,fy-y0,len(local),f'{max(xs)-min(xs):g} × {max(ys)-min(ys):g}',pitch(xs),pitch(ys),', '.join(sorted(set(f'{p[4]:g} × {p[5]:g}' for p in local))),', '.join(sorted(set(f'{p[6]:g}' for p in local))),f'{body[2]-body[0]:g} × {body[3]-body[1]:g}' if body else 'Не е зададен'])
parts.sort(key=lambda p:(re.sub(r'\d','',p[0]),int(re.search(r'\d+',p[0])[0])))
segments=[]
for i,s in enumerate(children(board,'segment'),1):
    a=pair(s,'start'); b=pair(s,'end')
    segments.append([i,field(s,'net',[''])[0],field(s,'layer')[0],float(field(s,'width')[0]),a[0]-x0,a[1]-y0,b[0]-x0,b[1]-y0,round(math.dist(a,b),4)])
for filename,heads,rows in [('components.csv',['ref','value','origin_x_mm','origin_y_mm','pads','pad_span_xy_mm','pitch_x_mm','pitch_y_mm','pad_size_mm','drill_mm','drawn_outline_mm'],parts),('pads.csv',['ref','pin','x_mm','y_mm','pad_x_mm','pad_y_mm','drill_mm','net'],pads),('tracks.csv',['segment','net','layer','width_mm','start_x_mm','start_y_mm','end_x_mm','end_y_mm','length_mm'],segments)]:
    with (OUT/filename).open('w',newline='',encoding='utf-8-sig') as f:
        writer=csv.writer(f);writer.writerow(heads);writer.writerows(rows)
def esc(s):return html.escape(str(s))
def svg(mode):
    items=[f'<svg xmlns="http://www.w3.org/2000/svg" width="277mm" height="190mm" viewBox="-20 -20 277 190">', '<rect x="-20" y="-20" width="277" height="190" fill="white"/>', '<defs><marker id="arrow" markerWidth="5" markerHeight="5" refX="2.5" refY="2.5" orient="auto-start-reverse"><path d="M5 0 L0 2.5 L5 5" fill="none" stroke="#333" stroke-width="0.6"/></marker></defs>', '<g font-family="Arial, sans-serif">']
    def line(a,b,color='#333',width=.2,extra=''):
        items.append(f'<line x1="{a[0]}" y1="{a[1]}" x2="{b[0]}" y2="{b[1]}" stroke="{color}" stroke-width="{width}" {extra}/>')
    def label(x,y,t,size=2.4):items.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="#172334">{esc(t)}</text>')
    items.append(f'<rect x="0" y="0" width="{w}" height="{h}" fill="#fafafa" stroke="#172334" stroke-width=".4"/>')
    for x in range(0,int(w)+1,10): line((x,0),(x,h),'#e0e6ed',.1);label(x, h+4,x,1.8)
    for y in range(0,int(h)+1,10): line((0,y),(w,y),'#e0e6ed',.1);label(-7,y,y,1.8)
    line((0,-8),(w,-8),extra='marker-start="url(#arrow)" marker-end="url(#arrow)"');label(w/2-8,-10,f'{w:g} mm',3)
    line((-12,0),(-12,h),extra='marker-start="url(#arrow)" marker-end="url(#arrow)"');label(-19,h/2,f'{h:g}',3)
    line((57,0),(57,h),'#b35b27',.5, 'stroke-dasharray="2 1"')
    label(2,6,'230 VAC',3)
    for ref,(a,b,c,d) in boxes.items():
        items.append(f'<rect x="{a-x0}" y="{b-y0}" width="{c-a}" height="{d-b}" fill="#e8f1fa" fill-opacity=".5" stroke="#718da7" stroke-width=".25"/>')
        label(a-x0+1,d-y0-1,f'{ref}: {c-a:g} × {d-b:g}',2)
    if mode!='placement':
        for s in segments:
            if s[2]==mode:line((s[4],s[5]),(s[6],s[7]),'#c13430' if mode=='F.Cu' else '#236dc0',s[3])
    for p in pads:
        ref,pin,x,y,sx,sy,drill,net=p
        items.append(f'<circle cx="{x}" cy="{y}" r="{sx/2}" fill="#e6c36e" stroke="#604921" stroke-width=".12"><title>{esc(ref)}.{esc(pin)}: X={x:g}; Y={y:g}; Ø{drill:g}; {esc(net)}</title></circle><circle cx="{x}" cy="{y}" r="{drill/2}" fill="white"/>')
    for p in parts:
        label(p[2]+1,p[3]-2,p[0],2.7)
    label(0,151,'Nano ESP32 — '+('РАЗПОЛОЖЕНИЕ' if mode=='placement' else mode+' — изглед отгоре'),3.5)
    label(0,156,'Всички размери в mm. X → надясно; Y ↓ надолу. (0,0) = горен ляв край.',2.5)
    label(0,161,'Контурите на модулите са от проекта; не са потвърдени габарити. Медта не е DRC проверена.',2.3)
    items.append('<rect x="222" y="5" width="10" height="10" fill="none" stroke="black" stroke-width=".2"/>');label(217,20,'10 × 10 mm',2.2)
    items.append('</g></svg>');return '\n'.join(items)
for mode,name in [('placement','PLACEMENT_DIMENSIONS_BG.svg'),('F.Cu','FRONT_TRACKS_DIMENSIONS_BG.svg'),('B.Cu','BACK_TRACKS_DIMENSIONS_BG.svg')]:
    (OUT/name).write_text(svg(mode))
def table(heads,rows):return '<table><thead><tr>'+''.join('<th>'+esc(h)+'</th>' for h in heads)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+esc(v)+'</td>' for v in row)+'</tr>' for row in rows)+'</tbody></table>'
summary=Counter((s[2],s[3]) for s in segments)
page='''<!doctype html><html lang="bg"><meta charset="utf-8"><title>Разположение и размери — Nano ESP32</title><style>body{font:14px Arial;margin:24px;color:#172334}h1{font-size:24px}h2{font-size:19px}table{border-collapse:collapse;width:100%;font-size:11px}td,th{border:1px solid #bac5d0;padding:5px;text-align:left}th{background:#edf2f7}img{max-width:100%}section{break-before:page}a{color:#236dc0}@page{size:A4 landscape;margin:10mm}@media print{body{margin:0} .drawing{width:277mm;height:190mm;max-width:none}section:has(.drawing) h2{display:none}tr{break-inside:avoid}}</style><h1>Разположение и размери — Nano ESP32 modular gateway rev A</h1>'''
page+=f'<p>Източник: действителният KiCad файл. Платка {w:g} × {h:g} mm, дебелина {field(children(board,"general")[0],"thickness")[0]} mm, два медни слоя. Координатите са от горния ляв край: X надясно, Y надолу. Началото в KiCad е ({x0:g}, {y0:g}) mm.</p>'
page+='<p>Печатайте SVG чертежите или тази страница на A4 landscape, 100%, без Fit to page. Проверете квадрата 10 × 10 mm. B.Cu е показан отгоре, без огледално обръщане; за тонер трансфер използвайте наличния B_Cu_MIRRORED_1TO1.pdf.</p>'
page+='<p><b>Това е размерен чертеж на съществуващия прототип, а не одобрение за производство.</b> Надписът „8 mm isolation“ е намерение в проекта: вертикалната линия не задава медна забранена зона. Не е извършена DRC или проверка на изолационните разстояния. Габаритите, височините и монтажните отвори на реалните модули трябва да се сверят.</p>'
for name,title in [('PLACEMENT_DIMENSIONS_BG.svg','Разположение и общи размери'),('FRONT_TRACKS_DIMENSIONS_BG.svg','Горни писти — F.Cu'),('BACK_TRACKS_DIMENSIONS_BG.svg','Долни писти — B.Cu')]:page+=f'<section><h2>{title}</h2><img class="drawing" src="{name}" alt="{title}"></section>'
page+='<section><h2>Всички елементи — размери в mm</h2><p>„Начало“ е началото на footprint, обикновено центърът на пин 1. Размахът е между крайните центрове на пиновете. Контурът е нарисуваното място за модула, а не измерен корпус. За F1, RV1, C1, SW1, TH1, R1 и клемите корпусите не са зададени в проекта.</p>'
page+=table(['Елемент','Стойност','X','Y','Пинове','Размах X × Y','Стъпки X','Стъпки Y','Площадка X × Y','Свредло Ø','Контур X × Y'],parts)
page+='<p>Монтажни отвори H1–H4: Ø3.2 mm, медни площадки Ø6 mm. Центровете са на 5 mm от краищата; между тях: 195 × 128 mm. В проекта отворите са метализирани (PTH).</p>'
page+='<h2>Ширини на пистите</h2>'+table(['Слой','Ширина mm','Брой сегменти'],[[layer,width,count] for (layer,width),count in sorted(summary.items())])
page+='<p>Сигналните писти и част от захранващите разклонения са 0.6 mm; горните захранващи разклонения са 1.0 mm; мрежовите и главните захранващи писти са 1.5 mm. Това са съществуващите ширини, без проверка на допустимия ток. Дебелината на медта не е специфицирана.</p>'
page+='<p>Пълните координати, отвори, електрически мрежи и дължини са в <a href="components.csv">components.csv</a>, <a href="pads.csv">pads.csv</a> и <a href="tracks.csv">tracks.csv</a>.</p></section>'
page+='<section><h2>Всички пинове и отвори</h2>'+table(['Елемент','Пин','X','Y','Площадка X','Площадка Y','Свредло Ø','Мрежа'],pads)+'</section>'
page+='<section><h2>Всички сегменти на пистите</h2>'+table(['№','Мрежа','Слой','Ширина','X начало','Y начало','X край','Y край','Дължина'],segments)+'</section></html>'
(OUT/'DIMENSIONS_BG.html').write_text(page)
assert (w,h)==(205,138)
assert len(parts)==19
assert all(p[6]>0 for p in pads)
print(f'Exported {len(parts)} components, {len(pads)} pads and {len(segments)} track segments to {OUT}')
print('Track widths:',sorted(summary.items()))
