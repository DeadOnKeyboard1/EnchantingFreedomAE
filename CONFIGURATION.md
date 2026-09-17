# Enchanting Freedom SE/AE configuration

The same DLL is designed for Skyrim 1.6.1170 and 1.7.104.0 through Address Library. Install the SKSE and Address Library packages matching the active game runtime.

The runtime configuration is:

`Data/SKSE/Plugins/EnchantingFreedomAE.ini`

## EnchantingFreedom

- `EnableEnchantmentSlotLimit`: enables the configurable enchantment slot limit.
- `EnchantmentMaxSlots`: accepts values from 1 through 50.
- `RemoveEnchantingRestrictions`: clears loaded ENCH worn restrictions in memory so non-excluded enchantments can be applied to valid enchantable items regardless of their original worn-restriction list.
- `RemoveDisenchantingRestrictions`: removes `MagicDisallowEnchanting` from loaded armor and weapon records.

## Exclusions

- `Enchantments`: keeps the original worn restrictions for selected enchantments.
- `Items`: keeps the original disenchanting restriction for selected items.

Exclusion entries use a hexadecimal local FormID and plugin name:

```ini
Enchantments=10FB7A|Skyrim.esm
```

Use `*` to exclude every matching record originating in one plugin:

```ini
Items=*|ExampleMod.esp
```

Multiple entries are comma-separated. Changes are read when Skyrim starts.
