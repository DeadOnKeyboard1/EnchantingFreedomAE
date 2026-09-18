# Changelog

## 1.3.0

- r13: fixed staff enchantments (for example the Staff of Magnus) reporting as learned but not appearing in the enchantment-effect list. `AddEnchantmentIfKnown` now categorizes non-excluded `SpellType::kStaffEnchantment` records as weapon effects without modifying the ENCH record.
- Build-Ready r9: reproducible build guard verifies the exact source SHA-256 before compilation and forces the exact pinned CommonLibSSE-NG revision instead of auto-selecting an arbitrary local toolchain checkout.
- Build-Ready r9: classifier now uses TESForm::Is(FormType) directly and contains no local FormType conversion/switch.
- Build-Ready r8: fixed the remaining MSVC multiple-inheritance cast errors in ClassifyEnchantingItem; keyword/weapon casts now use CommonLib `skyrim_cast`, and the FormType switch was replaced by equality checks.
- Build-Ready r7: fixed MSVC C3536/C2059/C2143 errors in the inventory classifier by replacing the problematic member-template expressions with FormType dispatch and non-template default-object lookup.
- Final r6 audit: known-enchantment bypass now checks the actual inventory ENCH in addition to Skyrim's resolved base ENCH, fixing exact-derived exclusion and `FFxxxxxx` protection bypasses.
- Final r6 audit: CommonLib trampoline patch-safety validation is enabled.
- Final r6 audit: release package name now states the actual supported runtimes (`1.6.1170` and `1.7.104`) instead of claiming SE/AE universal support.
- Reviewed r5 final: moved machine-code hook installation to SKSE `kPostPostLoad`, fixing too-early `GetPluginInfo()` conflict detection.
- Reviewed r5 final: direct `PluginVersionData` now advertises Address Library v5 plus post-1.6.629 structures instead of `UsesNoStructs`.
- Reviewed r5 final: known-enchantment hook now replaces only the `GetKnown()` result path, preserving other vanilla/third-party reasons an entry may be disabled.
- Reviewed r5 final: known-enchantment bypass now respects ENCH exclusions and keeps already-known `FFxxxxxx` transient forms protected by default.
- Reviewed r5 final: targeted staff hook now respects item/plugin exclusions.
- Reviewed r5 final: broad `AllowNonPlayableItems` classifier is limited to the directly verified 1.7.104 executable; 1.6.1170 falls back to safer targeted hooks.
- Reviewed r5: fixed Skyrim 1.7.104 / SKSE 2.3.1 plugin metadata by exporting `SKSE::PluginVersionData` with Address Library v5 compatibility instead of `SKSEPluginInfo`.
- Reviewed r5: default staff support now patches only the verified vanilla staff rejection branch; the 0xD0 classification replacement is reserved for `AllowNonPlayableItems=true`.
- Reviewed r5: broad 1.7.104 item-filter replacement verifies the complete original 0xD0-byte block before writing.
- Reviewed r5: `AllowDisenchantKnownEnchantments` no longer accidentally enables runtime-created `FFxxxxxx` enchantments.
- Reviewed r5: added experimental `AllowDisenchantCreatedEnchantments=false` for explicit opt-in.
- Reviewed r5: full classifier uses `InventoryEntryData::GetEnchantment()` to match vanilla classification more closely.
- Reviewed r5: build script rejects stale local CommonLibSSE-NG copies without 1.7.104 / Address Library v5 support.
- Reviewed r4: verified all critical 1.7.104 menu offsets directly against the supplied SkyrimSE.exe.
- Reviewed r4: added `AllowDisenchantKnownEnchantments=true`; already learned armor/weapon enchantments can now still be disenchanted without clearing the learned ENCH flag.
- Reviewed r4: full 1.7.104 item-filter replacement now validates quest, staff, unarmed, accept, known, reject and SkyUI anchor bytes before writing.
- Reviewed r4: 1.7.104 slot-limit constructor is verified at constructor +0x243 before patching.
- Reviewed r3: detects AmmoEnchanting and skips the conflicting full item-filter hook independent of DLL load order.
- Reviewed r3: fixes exact ENCH exclusions so excluding one derived enchantment no longer unintentionally excludes siblings sharing the same base.
- Reviewed r3: resolves the internal unarmed-weapon Address Library pointer during hook installation rather than from the runtime menu callback.
- Reviewed r3: preserves favorited quest items when optional quest-item support is enabled.
- Reviewed revision: restored vanilla filled-soul-gem classification after the expanded item-filter hook. Without this, soul gems could disappear from the enchanting menu.
- Reviewed revision: explicitly keeps Skyrim's internal unarmed pseudo-weapon out of the menu.
- Reviewed revision: advanced menu hooks are now restricted to exactly Skyrim 1.6.1170 and 1.7.104.0; other runtimes keep only the safer record-level patches.
- Replaced the v1.2.1 universal-keyword FormList workaround with `wornRestrictions = nullptr` for non-excluded ENCH records.
- Fixes armor enchantments such as Fortify Conjuration remaining unavailable on some gauntlets and modded equipment.
- Removes `MagicDisallowEnchanting` from loaded ARMO, WEAP and ENCH records.
- Added normal-enchanting-menu support for staves.
- Added optional non-playable item support.
- Added an opt-in quest-item bypass limited to EnchantConstructMenu.
- Added a Crafting Menu refresh pass for late runtime record modifications.
- Retained per-form/per-plugin exclusions.
- Added defensive hook checks so an already-modified item-filter block is not blindly overwritten.
- Added third-party attribution for documented hook strategies/locations used as references.
- Primary targets: Skyrim 1.6.1170 and 1.7.104.0.

## 1.2.1

- Fixed the startup regression introduced by 1.2.0 on Skyrim 1.7.104.0.
- Restored the strict v1.1 slot-hook matcher.
- Made slot-hook failure non-fatal.
- Used a universal loaded-keyword FormList for worn restrictions (superseded in 1.3.0).

## 1.2.0

- Reworked restriction removal around loaded ENCH records.
- Added exclusions.
