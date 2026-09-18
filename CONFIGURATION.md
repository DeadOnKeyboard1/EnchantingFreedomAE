# Enchanting Freedom AE 1.3.0 configuration

File:

`Data/SKSE/Plugins/EnchantingFreedomAE.ini`

## EnchantingFreedom

- `EnableEnchantmentSlotLimit=true`
  Enables the configurable enchantment-slot override.

- `EnchantmentMaxSlots=50`
  Maximum selected enchantments. Valid range: 1-50.

- `RemoveEnchantingRestrictions=true`
  Sets loaded, non-excluded ENCH `wornRestrictions` to null. This removes the normal armor/clothing slot-keyword restriction.

- `RemoveDisenchantingRestrictions=true`
  Removes `MagicDisallowEnchanting` from loaded, non-excluded ARMO, WEAP **and ENCH** records.

- `AllowDisenchantKnownEnchantments=true`
  Keeps DisenchantWeapon/DisenchantArmor entries enabled even when the underlying enchantment is already learned. The ENCH known flag is not cleared.

- `AllowDisenchantCreatedEnchantments=false`
  Experimental. Allows runtime-created `FFxxxxxx` enchantments to be disenchanted. Disabled by default because Skyrim deliberately filters these separately and `DisenchantItem` marks the ENCH learned before destroying the item. Ordinary ESP/ESM/ESL mod enchantments are unaffected by this protection.

- `AllowStaves=true`
  Includes staves in the normal weapon enchanting/disenchanting categories.

- `AllowNonPlayableItems=false`
  Allows hidden/non-playable ARMO/WEAP forms to enter the enchanting menu. This is disabled by default because many such forms were never intended to be player inventory items.

- `AllowQuestItems=false`
  Bypasses the EnchantConstructMenu quest-item exclusion. **Dangerous:** disenchanting destroys the selected item and may break quests.

- `RefreshOnCraftingMenuOpen=true`
  Re-applies data-record patches when the Crafting Menu opens so late runtime changes to already-loaded forms are corrected again.

## Exclusions

Comma-separated:

`hex-local-form-id|PluginName.esp`

or all matching records originating from one plugin:

`*|PluginName.esp`

Example:

```ini
[Exclusions]
Enchantments=10FB7A|Skyrim.esm,*|ProtectedEnchantments.esp
Items=1234|MyQuestMod.esp,*|DoNotTouch.esp
```

Excluded enchantments retain their original `wornRestrictions` and `MagicDisallowEnchanting`.

Excluded items retain their normal item-side restrictions and are not force-enabled by the staff/non-playable/quest-item options.


## Known hook compatibility

- `AmmoEnchanting`: detected automatically. It patches the same enchanting-menu classification region, so Enchanting Freedom AE skips staff/non-playable menu expansion when AmmoEnchanting is installed. Record-level restriction removal remains enabled.
- `Essential Favorites`: when quest-item support is enabled, favorited quest items stay protected.


## Hook timing and conflict safety

Menu-code hooks are installed at SKSE `kPostPostLoad`, not directly inside `SKSEPluginLoad`. This is important because `SKSE::GetPluginInfo()` is not reliably populated until SKSE has finished loading its plugin list.

The staff-only hook keeps `[Exclusions] Items=` active. `AllowNonPlayableItems=true` uses the broader 0xD0 classification replacement only on the directly verified Skyrim 1.7.104 executable; it is skipped on 1.6.1170.


## Final derived/runtime ENCH safety

For the already-known disenchant bypass, Skyrim has already resolved an enchantment to
its `baseEnchantment` before calling `GetKnown()`. The plugin therefore also reads the
actual enchantment from the selected `InventoryEntryData`. This keeps exact derived ENCH
exclusions and the default `FFxxxxxx` runtime-created enchantment protection effective.
