# Recipe Variants Framework

An SKSE plugin that lets a crafting recipe accept **every variant of an item**, not
just the one record it names.

A constructible object (COBJ) asks for exact forms: the vanilla Apple Cabbage Stew
wants the Red Apple record, and a Green Apple does not count. A COBJ cannot take a
form list, and a leveled item does not work there either. Mods that add variants of
an item (Skyrim Food Expansion has seven apples, nine tomatoes, seven garlics...)
have had one way around it: copy the recipe once per variant. The cooking menu then
lists the same stew six times, and a soup that takes a tomato *and* a garlic needs
one copy per pair -- 63 tomato soups.

With this framework the mod ships a JSON file saying which items are variants of
which base item. One recipe naming the base then accepts every variant, and the
copies the mod made per variant are folded away in memory at startup. Nothing is
patched on disk: without the framework, the mod's copies keep working as before.

## How it works

At startup, recipes that only differ by which variant of a group they ask for
(same result, same bench, no conditions, same items once each variant is read as
its base) are merged: one is kept, the others are moved to a bench keyword
(`RVF_MergedRecipe`) that no workbench carries, so they stop showing. When every
copy names a variant, the kept one is pointed at the base items. Copies that only
differ by which variant they *make* (one cutting recipe per half model) are folded
the same way, keeping the one that makes the base item (`MergeVariantResults`).
Recipes with conditions are never merged.

When the player activates a crafting bench (cooking pot, oven, forge...) -- before
the menu opens, since the menu builds its recipe list as it opens:

1. For every group that applies to the bench, the variants the player carries are
   swapped for the base item. Five tomatoes of three different kinds become five
   "Tomato" base items, so every recipe naming the base sees them.
2. The player crafts as usual.
3. When the crafting menu closes, whatever crafting did not use is swapped back to
   the original variants.

The swap is exact on counts: the number of items handed back never exceeds what is
there. Base items the player already owned count as used first, so a player who had
two vanilla potatoes and three variants and cooked one stew gets the three variants
back. When crafting both uses and makes the base item in one sitting (an oven baking
bread from a group), only the net change is visible; the count is still right, but
a variant may come back as a different variant of the same group.

Three rules keep it safe:

- **Stolen items stay stolen.** A swapped stack keeps its owner, and items given back
  take the owner of the base items they replace, so the number of stolen items never
  changes. The add-then-remove sequence with a copied owner is the one
  [YASTM](https://github.com/Seally/yastm) uses to swap soul gems.
- **A recipe is never taken away.** At startup the plugin reads every recipe in the
  load order. A variant that some recipe asks for by name (HearthFires' apple pie
  wants a Green Apple) is not swapped at that recipe's bench, so the recipe keeps
  working. The log names each such variant; point those recipes at the base to let
  every variant count there too.
- **Only plain stacks move.** A stack carrying anything but a count and an owner (a
  quest alias, a favourite hotkey, a script's unique ID) is left alone.

If the game is saved while a session is open (normal play cannot do that: the
crafting menu blocks saving), the co-save keeps the session and it is settled after
the next load.

## Config format

One file per mod, in `Data/SKSE/Plugins/RecipeVariantsFramework/`. Files load in
filename order; `//` comments are allowed.

```jsonc
{
  "$schema_version": 1,
  "groups": [
    {
      "name": "Garlic",                              // labels the group in the log
      "base": "Skyrim.esm|0x034D22",                  // the item recipes name
      "variants": [
        "My Food Mod.esp|0x000A8",                    // a whole bulb: 1 for 1
        { "form": "My Food Mod.esp|0x000AB", "per_base": 5 }   // 5 cloves = 1 bulb
      ],
      "workbench_keywords": ["CraftingCookpot", "BYOHCraftingOven"],
      "requires": "My Food Mod.esp",
      "convert_on_pickup": false
    }
  ]
}
```

| Key | Required | Meaning |
|---|---|---|
| `base` | yes | The item the recipes name. One group per base. |
| `variants` | yes | Items that count as the base. A string, or `{ "form", "per_base" }` when several variants make one base item (halves: 2, cloves: 5). Leftovers that do not make a whole base item stay as they are. |
| `name` | no | Label for the log. Defaults to `base`. |
| `workbench_keywords` | no | Keyword editor IDs. The group applies only at a bench with one of them. Default: every bench that crafts from recipes (create-object and smithing benches; alchemy and enchanting use none). |
| `requires` | no | Plugin name or list. The group is skipped (with a log line, not an error) unless all are loaded. |
| `convert_on_pickup` | no | `true` turns variants into the base item for good as soon as the player picks them up, instead of only at the bench. The variants' own names and models are lost from the inventory, so use it only for items where that does not matter. |

**Forms** are written `"Plugin.esp|0xID"` or `"0xID~Plugin.esp"` (the SPID/KID/BOS
order), with the ID local to its plugin (at most 6 hex digits). An editor ID also
works, but most item records only keep their editor ID at runtime with
[powerofthree's Tweaks](https://www.nexusmods.com/skyrimspecialedition/mods/51073)
installed; the plugin form always works.

A form can be the base of one group or a variant of one group, never both, and never
a variant of two groups. Conflicts are logged and the later entry is dropped.

The first prototype's shape, `{ "BaseEditorID": ["Variant", ...] }`, still loads,
with every option at its default.

## For mod authors

1. Write the groups (see `examples/` for Skyrim Food Expansion).
2. Run the report against your plugins, in load order:

   ```
   python tools/recipe_report.py MyGroups.json Skyrim.esm Update.esm Dawnguard.esm ^
       HearthFires.esm Dragonborn.esm "My Food Mod.esp"
   ```

   It lists the recipes per group, the recipes that become exact duplicates once
   variants count as the base (safe to delete), and the recipes that name a variant
   with no twin naming the base (point them at the base). It only reads files.
3. Make the framework a requirement. The duplicates are merged at runtime, so the
   plugin can keep them (and still work without the framework); delete them only if
   the framework becomes a hard requirement. Retarget the recipes the report lists
   as naming a variant with no twin: they are not duplicates, so they stay, and
   their variant is kept out of swaps at their bench.

## Skyrim Food Expansion

`examples/Skyrim Food Expansion/` holds a config built from Skyrim Food Expansion
3.1.1 and the vanilla masters, and `recipe_report.txt`, the report for it: 95 of
the mod's recipes become duplicates (98 with `MergeVariantResults`). Groups were only added where a recipe needs
them: the variants the mod already duplicates recipes for, plus carrot, bread and
salmon, whose variants cannot be used at all today (the vanilla beef stew, garlic
bread and grilled salmon only take the vanilla record).

Halves and cloves are inferred from the mod's own cutting recipes (one potato makes
two `Alax_Potato08`; the tomato soups take five `Alax_Garlic02` instead of one
bulb).

## Settings

`Data/SKSE/Plugins/RecipeVariantsFramework.ini`, rewritten on launch with any missing
key:

| Key | Default | |
|---|---|---|
| `[General] Enabled` | `true` | Master switch. |
| `[Recipes] MergeDuplicates` | `true` | Fold the per-variant recipe copies into one, in memory. |
| `[Recipes] MergeVariantResults` | `true` | Also fold copies that only differ by which variant they make; the other models no longer come out of that recipe. |
| `[Log] Level` | `info` | `debug` also lists every variant and every recipe that names one. |

The log is `Documents/My Games/Skyrim Special Edition/SKSE/RecipeVariantsFramework.log`.

## Building

ClibDT drives this with xmake; pick the runtime in its dropdown. From a shell:

```
xmake f -y -p windows -a x64 -m release --toolchain=msvc --skyrim_se=y
xmake build
```

The runtime flag is not optional: the project redeclares CommonLibSSE's
`skyrim_se` / `skyrim_ae` / `skyrim_vr` options with a `false` default, and without
one the library compiles against `UNKNOWN_RUNTIME`. Build each runtime on its own and
ship one DLL per runtime.

Requires CommonLibSSE-NG (`lib/commonlibsse-ng`, alandtse `ng`), ClibUtil (settings)
and nlohmann_json 3.12.0 (configs, fetched by xmake).

## Status

Compiles for SE, AE and VR. **Not yet verified in game.** To confirm on a first run,
with `Level = debug`:

1. that the log reports `Merged N duplicate recipe(s)` and the cooking pot shows one
   Tomato Soup, one Venison Stew...
2. that activating a cooking pot logs `Bench activated (main thread, menu closed)`
   then `Session start (workbench activated)`, and that a recipe naming the base
   lights up with only variants in the inventory. A warning `Session started after
   the crafting menu opened` means the swap still came too late; report it.
3. that closing the menu logs `Session end` with the right counts, and the variants
   are back in the inventory.
4. that a stolen variant is still stolen after a session, and that the engine
   merges the restored stolen items into one stack.
5. that the HearthFires apple pie still asks for, and accepts, a Green Apple at the
   oven.

Remove the first prototype (`InventorySwap.dll`) when installing this: both would
act on the same items.
