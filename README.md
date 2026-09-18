# Enchanting Freedom AE 1.3.0

Enchanting Freedom AE is an SKSE plugin that removes Skyrim's normal enchanting and disenchanting restrictions while keeping dangerous edge cases configurable.

## Runtime targets

Primary targets:

- Skyrim 1.6.1170
- Skyrim 1.7.104.0

The project uses SKSE, Address Library and CommonLibSSE-NG. The DLL also performs a load-time runtime gate and refuses runtimes other than 1.6.1170 and 1.7.104. Skyrim VR is not supported.

## What v1.3.0 changes

### Enchanting

- Clears `ENCH::wornRestrictions` for every loaded non-excluded enchantment.
- This removes armor/clothing slot restrictions. For example, an armor enchantment such as Fortify Conjuration is no longer limited to vanilla slot keywords and can be used on modded gauntlets/clothing that Skyrim would normally reject.
- Keeps Skyrim's fundamental armor-effect vs weapon-effect categories. Weapon enchantments are still weapon effects; armor enchantments are still armor effects.
- Configurable enchantment slot count from 1 through 50.

### Disenchanting

`AllowDisenchantKnownEnchantments=true` keeps armor/weapon entries enabled even when their ENCH is already learned. The ENCH remains marked as known; the plugin does not make the player forget anything.

Runtime-created `FFxxxxxx` enchantments are treated as a separate safety case. r5 keeps already-known transient ENCH forms protected by default with:

```ini
AllowDisenchantCreatedEnchantments=false
```

This does **not** block ordinary modded ESP/ESM/ESL enchantments. Setting it to `true` is an experimental opt-in because disenchanting a runtime-created enchantment causes Skyrim to mark that transient ENCH as learned before destroying the item.

When `RemoveDisenchantingRestrictions=true`, the plugin removes `MagicDisallowEnchanting` from:

- loaded `ARMO` records
- loaded `WEAP` records
- loaded `ENCH` records

Removing the keyword from ENCH records is important for many unique and modded enchantments where the protection is stored on the enchantment itself.

### Staves

`AllowStaves=true` removes only Skyrim's single vanilla staff-rejection branch. With the default `AllowNonPlayableItems=false`, v1.3.0 no longer replaces the complete item-classification block just to support staves.

If `AllowNonPlayableItems=true` is enabled manually, the broader classification hook is still required. It is intentionally available only on the supplied/verified 1.7.104 executable; on 1.6.1170 the option is skipped while the safer staff hook can still operate.

This is an engine-level menu extension and should be tested with the specific staff/enchantment combination. Mods with their own staff crafting systems or custom DLL logic can still impose separate rules.

### Modded content

The data pass uses `TESDataHandler` arrays, so normal ESP/ESM/ESL armor, weapons and enchantments are included automatically.

`RefreshOnCraftingMenuOpen=true` re-applies the record patches whenever the Crafting Menu opens. This helps if another DLL modifies already-loaded records after `DataLoaded`.

A mod that uses a completely custom crafting menu, custom runtime-only forms not registered in the normal data arrays, or its own DLL/script eligibility checks can still require a compatibility patch.

### Quest and non-playable items

These are deliberately protected by default:

```ini
AllowNonPlayableItems=false
AllowQuestItems=false
```

They can be enabled manually. **Disenchanting destroys the selected item. Enabling quest items can permanently break quests.**

The quest-item bypass is limited to the quest check used by `EnchantConstructMenu`; it does not globally disable quest-item protection elsewhere in Skyrim.

## Important scope

"Freedom" means:

- armor enchantments can be applied to any enchantable armor/clothing item regardless of the original worn-slot restriction;
- weapon enchantments can be applied to normal weapons and, when enabled, staves;
- `MagicDisallowEnchanting` protection is removed from items and enchantments;
- normal modded ARMO/WEAP/ENCH records are processed automatically.

It does **not** convert armor enchantments into weapon enchantments or vice versa. That is a separate category system inside Skyrim's enchanting menu and changing it would require a much more invasive rewrite of the effect/filter/crafting paths.

Already-known static enchantments remain learned but can be disenchanted when `AllowDisenchantKnownEnchantments=true`. Runtime-created `FFxxxxxx` enchantments remain protected by default. The final audit checks the actual inventory ENCH, not only Skyrim's resolved base ENCH, so derived exclusions and FF-form protection are preserved.

## Compatibility

On the default configuration, staff support replaces only the six-byte conditional branch that rejects staves with a small trampoline; the trampoline also honors `[Exclusions] Items=` for individual or plugin-wide staff exclusions. The complete `EnchantConstructMenu::PopulateEntryList` item-classification block is replaced only when `AllowNonPlayableItems=true`.

On Skyrim 1.7.104, the broader hook verifies the complete original 0xD0-byte block (FNV-1a plus independent anchors) before writing it. If another DLL changed any byte in that block, Enchanting Freedom AE skips the hook instead of overwriting foreign machine code. Record-level restriction removal still works.

`AmmoEnchanting` is a known hard conflict at this exact block. Hook installation is deferred until SKSE `kPostPostLoad`, when plugin metadata is actually available; v1.3.0 can therefore detect AmmoEnchanting independent of DLL load order and skip the conflicting staff/non-playable expansion. ARMO/WEAP/ENCH record patches remain active.

The optional quest-item hook chains the call currently installed at the vanilla quest-check call site where possible.

## Skyrim 1.7.104 loader metadata

The DLL exports `SKSE::PluginVersionData` directly instead of the older `SKSEPluginInfo` / `PluginDeclaration` wrapper. This explicitly carries the Address Library v5 compatibility bit used by SKSE 2.3.1 / Address Library v13 on Skyrim 1.7.104, and advertises post-1.6.629 gameplay structures.

The build script also rejects an automatically discovered local CommonLibSSE-NG checkout if it does not expose 1.7.104 and Address Library v5 support.

## Build

Double-click `Build.cmd`, or run:

```powershell
.\Build-EnchantingFreedomAE.ps1 -Clean
```

Expected package:

`dist\EnchantingFreedomAE-1.3.0-AE-1.6.1170-1.7.104.zip`

## License and attribution

Enchanting Freedom AE is GPL-3.0-or-later.

v1.3.0 uses documented hook locations/behavior informed by GPL/MIT open-source Skyrim projects. See `THIRD_PARTY_NOTICES.md`.


## 1.7.104 executable verification

Reviewed r5 was checked directly against the supplied `SkyrimSE.exe` 1.7.104.0
(SHA-256 `846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f`).

Verified in that executable:

- `EnchantConstructMenu::PopulateEntryList` RVA `0x9212B0`
- quest check call at `+0x140`
- item classification block at `+0x14D`
- vanilla staff exclusion at `+0x1A9`
- internal unarmed weapon comparison at `+0x1D3`
- item-filter accept continuation at `+0x21D`
- `TESForm::GetKnown()` check sequence at `+0x269`
- reject continuation at `+0x314`
- SkyUI-related anchor at `+0x4A6`
- `EnchantConstructMenu` constructor RVA `0x91B010`
- enchantment selection-limit write at constructor `+0x243`

The 1.7.104 broad item-filter fallback verifies the entire 0xD0-byte block before modifying it. The normal staff-only path changes only the verified branch at `+0x1B0`.


### Remaining engine edge cases

Staff support is intentionally still marked as an engine edge case. The vanilla enchanting table excludes staves entirely, and some unique/modded staff enchantments are not designed to become ordinary learned weapon enchantments. The hook itself is guarded, but a specific mod can still create a semantically unusable learned enchantment without necessarily causing a DLL crash.

Re-enchanting an already-enchanted item and mixing weapon enchantments with armor enchantments (or vice versa) are not implemented by v1.3.0. Those paths require deeper crafting rewrites than the compatibility-focused hooks in this release.


## Staff enchantments in the learned effect list (r13)

Skyrim's `AddEnchantmentIfKnown` only exposes `ConstantEffect + Self` as an
armor effect and `FireAndForget + Touch` as a weapon effect. Staff enchantments
can use `SpellType::kStaffEnchantment` with staff-specific casting/delivery, so
the game may report the enchantment as learned but omit it from the effect list.

With `AllowStaves=true`, r13 normalizes only those four category-query calls
inside `AddEnchantmentIfKnown`. A non-excluded staff enchantment is presented to
the menu as `FireAndForget + Touch`, making it an `EffectWeapon`. The ENCH record
itself is not modified.
