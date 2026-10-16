# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Alaxouche

"""Check a mod's recipes against an RecipeVariantsFramework config.

With the framework, one recipe naming the base item accepts every variant of its
group. This reads the plugins (in load order) and the config, and reports:

  * per group, which recipes use the base and which use a variant;
  * recipes that become exact duplicates once variants count as the base --
    the copies the framework makes unnecessary, safe to delete;
  * recipes that name a variant and have no twin naming the base -- retarget
    them to the base (or keep them, if that variant is really required).

    python tools/recipe_report.py config.json Skyrim.esm Update.esm \\
        Dawnguard.esm HearthFires.esm Dragonborn.esm "My Food Mod.esp"

Only plugin files are read; nothing is written. Conditions are compared byte for
byte, so two recipes from different plugins with the same conditions may not be
recognised as duplicates -- review the output, it is a checklist, not a patch.
"""
import argparse
import collections
import json
import os
import re
import struct

import esp


def load(paths):
    """Winning records by (plugin, local id), plus editor IDs and names."""
    records = {}      # key -> (sig, data, file)
    editor_ids = {}   # lowercase editor id -> key
    for path in paths:
        buf, masters, start = esp.read(path)
        name = os.path.basename(path)
        # Localized plugins (the official masters) store names as string-table
        # IDs, not text; only the editor ID is readable without the .strings files.
        localized = bool(struct.unpack_from('<I', buf, 8)[0] & 0x80)

        def key(formid, masters=masters, name=name):
            index = formid >> 24
            plugin = masters[index] if index < len(masters) else name
            return plugin.lower(), formid & 0xFFFFFF

        for sig, formid, data, _ in esp.walk(buf, start, len(buf)):
            if sig not in (b'COBJ', b'ALCH', b'INGR', b'MISC', b'KYWD'):
                continue
            k = key(formid)
            subs = list(esp.subrecords(data))
            records[k] = (sig, subs, key, localized)
            for s, d in subs:
                if s == b'EDID':
                    editor_ids[esp.zstring(d).lower()] = k
    return records, editor_ids


def field(subs, sig):
    return next((d for s, d in subs if s == sig), None)


def describe(records, k):
    rec = records.get(k)
    if not rec:
        return f'{k[1]:06X}:{k[0]}'
    edid, full = field(rec[1], b'EDID'), field(rec[1], b'FULL')
    label = esp.zstring(edid) if edid else f'{k[1]:06X}'
    return f'{label} "{esp.zstring(full)}"' if full and not rec[3] else label


def parse_form(text, editor_ids):
    """Plugin.esp|0x123, 0x123~Plugin.esp or an editor ID -> record key."""
    parts = re.split(r'[|~]', text)
    if len(parts) == 2:
        a, b = (p.strip() for p in parts)
        plugin, local = (a, b) if a.lower().endswith(('.esp', '.esm', '.esl')) else (b, a)
        return plugin.lower(), int(local, 16)
    return editor_ids.get(text.strip().lower())


def read_jsonc(path):
    """JSON with // comments, which the plugin accepts too."""
    text = open(path, encoding='utf-8').read()
    text = re.sub(r'("(?:\\.|[^"\\])*")|//[^\n]*', lambda m: m.group(1) or '', text)
    return json.loads(text)


def load_groups(config, editor_ids):
    doc = read_jsonc(config)
    entries = doc.get('groups')
    if entries is None:  # shorthand: { "Base": ["Variant", ...] }
        entries = [{'base': b, 'variants': v} for b, v in doc.items() if not b.startswith('$')]
    groups = []
    for entry in entries:
        base = parse_form(entry['base'], editor_ids)
        variants = {}
        for v in entry['variants']:
            form, per_base = (v, 1) if isinstance(v, str) else (v['form'], v.get('per_base', 1))
            k = parse_form(form, editor_ids)
            if k is None:
                print(f'  ! unresolved variant {form!r} in group {entry.get("name", entry["base"])!r}')
                continue
            variants[k] = per_base
        groups.append({'name': entry.get('name', entry['base']), 'base': base, 'variants': variants})
    return groups


def recipe(subs, key):
    """(created, count, bench, conditions, ingredients) of a COBJ."""
    created = field(subs, b'CNAM')
    bench = field(subs, b'BNAM')
    count = field(subs, b'NAM1')
    items = collections.Counter()
    for s, d in subs:
        if s == b'CNTO':
            formid, n = struct.unpack_from('<Ii', d)
            items[key(formid)] += n
    conditions = b''.join(d for s, d in subs if s in (b'CTDA', b'CIS1', b'CIS2'))
    return (key(struct.unpack('<I', created)[0]) if created else None,
            struct.unpack('<H', count)[0] if count else 1,
            key(struct.unpack('<I', bench)[0]) if bench else None,
            conditions, items)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('config')
    parser.add_argument('plugins', nargs='+', help='plugin files, in load order')
    args = parser.parse_args()

    records, editor_ids = load(args.plugins)
    groups = load_groups(args.config, editor_ids)

    to_base = {}  # variant key -> (base key, per_base)
    for g in groups:
        for v, per_base in g['variants'].items():
            to_base[v] = (g['base'], per_base)
    member = {g['base']: g for g in groups}
    member.update({v: g for g in groups for v in g['variants']})

    cobjs = {k: recipe(rec[1], rec[2]) for k, rec in records.items() if rec[0] == b'COBJ'}

    print('== Recipes per group')
    for g in groups:
        users = collections.defaultdict(list)
        for k, (created, n, bench, cond, items) in cobjs.items():
            for item in items:
                if item == g['base']:
                    users['base'].append(k)
                elif item in g['variants']:
                    users['variant'].append(k)
        print(f"\n{g['name']}: base {describe(records, g['base'])}, {len(g['variants'])} variant(s)")
        print(f"   {len(users['base'])} recipe(s) name the base, {len(users['variant'])} name a variant")

    def canonical(items):
        out = collections.Counter()
        for item, n in items.items():
            base, per_base = to_base.get(item, (item, 1))
            if n % per_base:
                return None  # not a whole number of base items: leave it alone
            out[base] += n // per_base
        return tuple(sorted(out.items()))

    # A result that is a variant counts as its base (the plugin's
    # MergeVariantResults): one cutting recipe per half model is still a copy.
    clusters = collections.defaultdict(list)
    for k, (created, n, bench, cond, items) in cobjs.items():
        if any(i in member for i in items) or created in member:
            c = canonical(items)
            if c is not None:
                result = to_base.get(created, (created, 1))[0]
                clusters[(result, n, bench, cond, c)].append(k)

    def uses_only_bases(k):
        return not any(i in to_base for i in cobjs[k][4])

    def makes_base(k):
        return cobjs[k][0] not in to_base

    print('\n== Duplicates once variants count as the base')
    removable = 0
    for (created, n, bench, cond, c), ks in sorted(clusters.items(), key=lambda kv: describe(records, kv[0][0])):
        if len(ks) < 2:
            continue
        keep = next((k for k in ks if uses_only_bases(k) and makes_base(k)), None) or             next((k for k in ks if uses_only_bases(k)), None)
        print(f'\n{describe(records, created)} x{n}: {len(ks)} recipes')
        if keep is None:
            print('   none names only base items: retarget one to the bases, delete the rest')
        for k in ks:
            tag = 'keep' if k == keep else 'delete'
            removable += k != keep
            print(f'   {tag:6s} {k[1]:06X}:{k[0]}  {describe(records, k)}')
    print(f'\n{removable} recipe(s) can go.')

    print('\n== Recipes naming a variant with no base twin (retarget to the base)')
    for (created, n, bench, cond, c), ks in clusters.items():
        if len(ks) == 1 and not uses_only_bases(ks[0]):
            k = ks[0]
            names = ', '.join(describe(records, i) for i in cobjs[k][4] if i in to_base)
            print(f'   {k[1]:06X}:{k[0]}  {describe(records, k)}  <- {names}')


if __name__ == '__main__':
    main()
