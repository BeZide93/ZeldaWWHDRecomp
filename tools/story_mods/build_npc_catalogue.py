#!/usr/bin/env python3
"""Builds the Windfall table of the NPC catalogue (runtime/assets/npc_catalogue/windfall.json) statically.

Reads the user's own game files locally (never copied anywhere) and writes only derived facts:
  - who stands where: Windfall's room actor lists (sea room 11 and the interiors its doors lead to),
    from the room archives' .dzr files (stage, room, layer, position);
  - names: the Nintendo Gallery figurine name message of each actor's pictograph type. The decompilation
    ties them: the actor registers its pictograph type (dSnap_RegistFig), the figure number is the type
    minus 0x49 (dSnap_PhotoIndex2TableIndex) and the figurine's message is 0x37DD + figure
    (daObjFigure_c::getMsg). Only the name is written, never message text.
  - presence: the room layers (dComIfG_play_c::getLayerNo) and, for the townspeople, the conditions in
    daNpcPeople_c phase_1; for other classes these are marked unconfirmed.

usage: build_npc_catalogue.py --game <game dir with content/> --out runtime/assets/npc_catalogue/windfall.json
"""
import argparse, json, os, re, struct, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import shaderprep

ap = argparse.ArgumentParser()
ap.add_argument('--game', required=True)
ap.add_argument('--out', required=True)
args = ap.parse_args()
base = os.path.join(args.game, 'content', 'Common') + '/'

def parse(d):
    bom = d[8:10]; e = '>' if bom == b'\xfe\xff' else '<'
    nsec = struct.unpack(e+'H', d[0xE:0x10])[0]
    o = 0x20; secs = {}
    for _ in range(nsec):
        mag = d[o:o+4]; size = struct.unpack(e+'I', d[o+4:o+8])[0]
        secs[mag] = d[o+0x10:o+0x10+size]; o = (o + 0x10 + size + 15) & ~15
    lbl = secs[b'LBL1']; n = struct.unpack(e+'I', lbl[:4])[0]
    labels = {}
    for i in range(n):
        cnt, off = struct.unpack(e+'II', lbl[4+8*i:12+8*i])
        p = off
        for _ in range(cnt):
            L = lbl[p]; name = lbl[p+1:p+1+L].decode(); idx = struct.unpack(e+'I', lbl[p+1+L:p+5+L])[0]
            labels[idx] = name; p += 5 + L
    txt = secs[b'TXT2']; m = struct.unpack(e+'I', txt[:4])[0]
    offs = [struct.unpack(e+'I', txt[4+4*i:8+4*i])[0] for i in range(m)] + [len(txt)]
    out = {}
    for i in range(m):
        raw = txt[offs[i]:offs[i+1]]
        s = []; j = 0
        while j + 1 < len(raw):
            c = struct.unpack(e+'H', raw[j:j+2])[0]
            if c == 0x0E:  # control: group, type, size, params
                g, t, sz = struct.unpack(e+'HHH', raw[j+2:j+8]); j += 8 + sz; s.append('<%d.%d>' % (g, t)); continue
            if c == 0: break
            s.append(chr(c)); j += 2
        out[labels.get(i, str(i))] = ''.join(s)
    return out

msgs = {}
for name, data in shaderprep.sarc(open(base + 'Pack/permanent_2d_UsEnglish.pack', 'rb').read()):
    if name.startswith('message'):
        for n2, d2 in shaderprep.walk(data, name):
            if n2.endswith('.msbt'):
                for k, v in parse(d2).items(): msgs[k] = (n2.split('/')[-1], v)

def dzrs(f):
    s32=lambda o:struct.unpack_from('>i',f,o)[0]
    g=0x20+4*11
    if s32(g)==0: return
    go=g+s32(g); n=s32(go+4)
    for i in range(1,n+1):
        e=go+8+16*i; no=e+8+s32(e+8); nm=f[no:f.index(b'\0',no)].decode(); do=e+12+s32(e+12)
        off=do+s32(do); size=struct.unpack_from('>I',f,do+4)[0]
        if nm.endswith('.dzr') or nm.endswith('.dzs'): yield nm,f[off:off+size]

def scan(stage,room,d):
    n=struct.unpack_from('>I',d,0)[0]
    for i in range(n):
        t,c,o=struct.unpack_from('>4sII',d,4+12*i); t=t.decode(errors='replace')
        if t.startswith('ACT') or t.startswith('TRE') or t.startswith('SCO'):
            sz=0x24 if t.startswith('SCO') else 0x20
            for k in range(c):
                e=o+sz*k; name=d[e:e+8].split(b'\0')[0].decode(errors='replace')
                prm=struct.unpack_from('>I',d,e+8)[0]; x,y,z=struct.unpack_from('>3f',d,e+12); ry=struct.unpack_from('>h',d,e+0x1A)[0]
                acts.append(dict(stage=stage,room=room,layer=t,name=name,prm=prm,x=round(x),y=round(y),z=round(z),ry=ry))
def handle(stage,label,szs):
    for n2,d2 in shaderprep.walk(szs,label):
        if n2.endswith('.bfres'):
            m=re.search(r'Room(\d+)',n2); room=int(m.group(1)) if m else -1
            for nm,dz in dzrs(d2): scan(stage,room,dz)

acts = []
STAGES = ['sea', 'Obombh', 'Orichh', 'Pdrgsh', 'Pnezumi', 'Opub', 'Kaisen', 'Ocmera', 'Nitiyou']  # sea room 11's doors (its SCLS list)
import glob
for f in sorted(glob.glob(base + 'Stage/*_Room*.szs')):
    st_ = os.path.basename(f).split('_Room')[0]
    if st_ in STAGES: handle(st_, os.path.basename(f), open(f, 'rb').read())
for p in ['szs_permanent0.pack', 'szs_permanent1.pack', 'szs_permanent2.pack', 'first_szs_permanent.pack']:
    for name, data in shaderprep.sarc(open(base + 'Pack/' + p, 'rb').read()):
        st_ = name.split('_Room')[0]
        if '_Room' in name and st_ in STAGES: handle(st_, name, data)

def fig_name(photo):
    no = photo - 0x49
    t = msgs['%05d' % (0x37DD + no)][1].split('\n')[0]
    return re.sub(r'<[\d.]+>', '', t).strip(), 0x37DD + no, no
E = []
PEOPLE = ["Uo1","Uo2","Uo3","Ub1","Ub2","Ub3","Ub4","Uw1","Uw2","Um1","Um2","Um3","Sa1","Sa2","Sa3","Sa4","Sa5","Ug1","Ug2"]
PHOTO = [104,105,106,107,107,108,109,102,103,100,99,101,130,110,111,113,112,98,98]  # d_a_npc_people l_photo_no
# entries: name, named, class, stage actor names, photo no, how the photo no is reached, where, gate
E = []
def people(code, name, where, gate=None, named=True, pair=None):
    n = PEOPLE.index(code)
    fname, msg, fig = fig_name(PHOTO[n])
    ev = ("Nintendo Gallery figurine name \"%s\" (msg 0x%04X): stage actor %s = daNpcPeople_c resident %d (d_stage OBJNAME), "
          "l_photo_no[%d] = 0x%02X (d_a_npc_people), figure %d = 0x%02X - 0x49 (dSnap_PhotoIndex2TableIndex), "
          "daObjFigure_c::getMsg = 0x37DD + figure" % (fname, msg, code, n, n, PHOTO[n], fig, PHOTO[n]))
    if pair: ev += "; the figurine shows a pair, which of the two this actor is isn't tied, so it is unnamed"
    E.append(dict(name=name, named=named, evidence=ev, cls='people', codes=[code], where=where, gate=gate))
def other(cls, codes, photo, how, name, where, gate=None, named=True, group=None):
    fname, msg, fig = fig_name(photo)
    ev = ("Nintendo Gallery figurine name \"%s\" (msg 0x%04X): %s registers pictograph type 0x%02X (%s), figure %d = 0x%02X - 0x49, "
          "daObjFigure_c::getMsg = 0x37DD + figure" % (fname, msg, '/'.join(codes), photo, how, fig, photo))
    if group: ev += "; the figurine shows the whole group, the members aren't tied one by one, so they are unnamed"
    E.append(dict(name=name, named=named, evidence=ev, cls=cls, codes=codes, where=where, gate=gate))
people('Uo1', 'windfall-sam', 'Windfall Island, on the sea-view bench at the cliff')
people('Uo2', 'windfall-gossack', 'Windfall Island, in the Café Bar')
people('Uo3', 'windfall-garrickson', 'Windfall Island, walking through town')
people('Ub1', 'windfall-gossip-a', 'Windfall Island, up the steps by the brick house', named=False, pair=True)
people('Ub2', 'windfall-gossip-b', 'Windfall Island, up the steps by the brick house', named=False, pair=True)
people('Ub3', 'windfall-missy', 'Windfall Island, on the harbour pier')
people('Ub4', 'windfall-minenco', 'Windfall Island, by the town gate', gate="in Lenzo's studio only until Link has Treasure Chart 20 and only at the entrance he came through (d_a_npc_people phase_1)")
people('Uw1', 'windfall-gillian', 'Windfall Island, in the Café Bar')
people('Uw2', 'windfall-linda', 'Windfall Island, the town square or the Café Bar', gate="where she stands follows her story with Anton: event register 0xB907 (d_a_npc_people phase_1, arg0)")
people('Um1', 'windfall-kreeb', 'Windfall Island, up on the windmill by the light')
people('Um2', 'windfall-anton', 'Windfall Island, the town square or the Café Bar', gate="where he stands follows his story with Linda: event register 0xB907 (d_a_npc_people phase_1, arg0)")
people('Um3', 'windfall-kamo', 'Windfall Island, on the steps by the red door (up high at night)')
people('Sa1', 'windfall-loot', 'Windfall Island, in the Café Bar at night')
people('Sa2', 'windfall-gummy', 'Windfall Island, on the high lookout')
people('Sa3', 'windfall-kane', 'Windfall Island, on the stone dock')
people('Sa4', 'windfall-candy', 'Windfall Island, at the end of the wooden pier')
people('Sa5', 'windfall-dampa', 'Windfall Island, on the grass by the stone wall')
people('Ug1', 'windfall-tipster-a', 'Windfall Island, by the flowers at the cliff', named=False, pair=True,
       gate="by the flowers except on days 5 and 6 of the game's week, in Mrs. Marie's school on those days (d_a_npc_people phase_1, dKy_get_dayofweek)")
people('Ug2', 'windfall-tipster-b', 'Windfall Island, by the flowers at the cliff', named=False, pair=True,
       gate="by the flowers except on days 5 and 6 of the game's week, in Mrs. Marie's school on those days (d_a_npc_people phase_1, dKy_get_dayofweek)")
other('npc_rsh1', ['Rsh1'], 0x5F, 'dSnap_RegistFig in d_a_npc_rsh1', 'windfall-zunari', "Windfall Island, Zunari's stall")
other('npc_tt', ['Tt'], 0x57, 'dSnap_RegistFig in d_a_npc_tt', 'windfall-tott', 'Windfall Island, dancing on the hill')
other('npc_kk1', ['Kk1'], 0x58, 'dSnap_RegistFig in d_a_npc_kk1', 'windfall-mila', 'Windfall Island', gate='story-gated (her own create conditions are not decoded here)')
other('npc_gk1', ['Gk1', 'Kf1'], 0x59, 'dSnap_RegistFig in d_a_npc_gk1 and d_a_npc_kf1', "windfall-milas-father", 'Windfall Island', gate='story-gated: the rich house before his fall, the town and the Café Bar after it')
other('npc_kp1', ['Kp1'], 0x5A, 'dSnap_RegistFig in d_a_npc_kp1', 'windfall-maggie', 'Windfall Island, the rich house', gate='story-gated')
other('npc_pf1', ['Pf1', 'Gp1'], 0x5B, 'dSnap_RegistFig in d_a_npc_pf1 and d_a_npc_gp1', "windfall-maggies-father", 'Windfall Island', gate='story-gated: the town before his fortune, the rich house after it')
other('npc_bms1', ['Bms1', 'Bms2'], 0x5C, 'dSnap_RegistFig_l in d_a_npc_bms1', 'windfall-cannon', 'Windfall Island, the bomb shop')
other('npc_ds1', ['Ds1'], 0x5D, 'dSnap_RegistFig in d_a_npc_ds1', 'windfall-doc-bandam', "Windfall Island, Doc Bandam's shop")
other('npc_photo', ['Po'], 0x5E, 'dSnap_RegistFig in d_a_npc_photo', 'windfall-lenzo', "Windfall Island, Lenzo's studio")
other('npc_ho', ['Ho'], 0x60, 'dSnap_RegistFig in d_a_npc_ho', 'windfall-mrs-marie', "Windfall Island, Mrs. Marie's school")
other('npc_kg1', ['Kg1'], 0x81, 'dSnap_RegistFig in d_a_npc_kg1', 'windfall-salvatore', 'Windfall Island, the battleship game')
other('npc_so', ['NpcSo'], 0x7D, 'dSnap_RegistFig in d_a_npc_so', 'windfall-fishman', 'the sea by Windfall Island')
other('npc_tc', ['Tc'], 0x79, 'dSnap_RegistFig in d_a_npc_tc, type from param & 0xF = 0xF -> normal', 'windfall-tingle', 'Windfall Island, the jail')
other('npc_bm4', ['Bm4'], 0x90, 'dSnap_RegistFig in d_a_npc_bm1_b, type param & 0xFF = 1/2 -> Ilari (decideType)', 'windfall-ilari', 'Windfall Island, the Café Bar and the auction house')
other('npc_zl1', ['Zl1'], 0x72, 'dSnap_RegistFig in d_a_npc_zl1', 'windfall-tetra', 'Windfall Island, the bomb shop', gate='story-gated: the bomb shop break-in (conditions not decoded here)')
other('npc_p1', ['P1a'], 0x73, 'l_snap_idx_tbl[type] in d_a_npc_p1, P1a = type 0', 'windfall-gonzo', 'Windfall Island, the bomb shop', gate='story-gated: the bomb shop break-in')
other('npc_p1', ['P1b'], 0x74, 'l_snap_idx_tbl[type] in d_a_npc_p1, P1b = type 1', 'windfall-senza', 'Windfall Island, the bomb shop', gate='story-gated: the bomb shop break-in')
other('npc_p1', ['P1c'], 0x75, 'l_snap_idx_tbl[type] in d_a_npc_p1, P1c = type 2', 'windfall-nudge', 'Windfall Island, the bomb shop', gate='story-gated: the bomb shop break-in')
for i, c in enumerate(['Mk', 'UkB', 'UkC', 'UkD']):
    other('npc_mk' if c == 'Mk' else 'npc_uk', [c], 0x61, 'dSnap_RegistFig in d_a_npc_%s' % ('mk' if c == 'Mk' else 'uk'),
          'windfall-killer-bee-' + 'abcd'[i], 'Windfall Island, around town', named=False, group=True)

# ---- instances from the room data, presence, the catalogue file
STAGE_WHERE = {'sea': 'outside', 'Opub': 'the Café Bar', 'Ocmera': "Lenzo's studio", 'Orichh': 'the rich house (auction)',
               'Obombh': 'the bomb shop', 'Pdrgsh': "Doc Bandam's shop", 'Nitiyou': "Mrs. Marie's school", 'Kaisen': 'the battleship game',
               'Pnezumi': 'the jail'}
def layer_no(t): return 'R' if t == 'ACTR' else int(t[3], 16)
out = []
for e in E:
    inst = {}
    for a in acts:
        if a['name'] not in e['codes'] or not a['layer'].startswith('ACT'): continue
        if a['stage'] == 'sea' and a['room'] != 11: continue
        if a['stage'] == 'sea' and a['name'] in ('NpcSo',): pass
        key = (a['name'], a['stage'], a['room'])
        l = layer_no(a['layer'])
        rec = inst.setdefault(key, {'layers': set(), 'spots': []})
        rec['layers'].add(l)
        rec['spots'].append((l, a['x'], a['z']))
    actors = []
    day = night = False
    spot = None
    spot_by_day = False
    for (code, stage, room), rec in sorted(inst.items(), key=lambda kv: (kv[0][1] != 'sea', kv[0])):
        layers = rec['layers']
        ls = sorted(layers, key=str)
        outdoor = stage == 'sea'
        dl = {0, 4, 'R'} if outdoor else {0, 'R'}
        nl = {1, 3, 5, 'R'} if outdoor else {1, 'R'}
        if layers & dl: day = True
        if layers & nl: night = True
        if not (layers & (dl | nl)): continue  # only in layers the game doesn't choose (demo layers)
        item = {'class': e['cls']}
        if e['cls'] == 'people': item['code'] = code
        else: item['actor'] = code
        item.update({'stage': stage, 'room': room, 'layers': [str(l) for l in ls]})
        actors.append(item)
        if outdoor:
            by_day = [sp for sp in rec['spots'] if sp[0] in (0, 4, 'R')]
            sp = (by_day or rec['spots'])[0]
            if spot is None or (by_day and not spot_by_day): spot, spot_by_day = {'x': sp[1], 'z': sp[2]}, bool(by_day)
    entry = {'name': e['name'], 'since': 2, 'named': e['named'], 'evidence': e['evidence'], 'where': e['where']}
    if spot: entry['spot'] = spot
    entry['present'] = [p for p, v in (('day', day), ('night', night)) if v]
    if e['gate']: entry['gate'] = e['gate']
    if e['cls'] != 'people':
        entry['unconfirmed'] = ("when it is out: only from the room layers; the actor's own create conditions "
                                "(d_a_%s) are not decoded" % e['cls'])
    entry['actors'] = actors
    if not actors: print('NO INSTANCES', e['name'], file=sys.stderr); continue
    out.append(entry)
doc = {'catalogue_format': 2, 'place': 'windfall', 'title': 'Windfall Island',
       'layers': {'sea room 11': '0 day, 1 night, 3 the Endless Night, 4 day and 5 night once event flag 0x2D01 is set (dComIfG_play_c::getLayerNo); R every layer',
                  'interiors': '0 day, 1 night (also the Endless Night); R every layer'},
       'npcs': out}
json.dump(doc, open(args.out, 'w'), indent=2, ensure_ascii=False)
print(len(out), 'entries')
