# Changelog

## 1.2.1

- Fixed a startup regression introduced by 1.2.0 on Skyrim 1.7.104.0.
- Restored the strict v1.1 slot-limit instruction matcher to avoid false-positive code matches.
- A slot-limit hook failure is now logged instead of aborting SKSE plugin loading.
- Replaced the incorrect `wornRestrictions = nullptr` logic with an in-memory FormList containing all loaded keywords.
- Added exception containment around the DataLoaded patch stage so a data-patch error cannot take down game startup.
- Primary targets remain Skyrim 1.6.1170 and 1.7.104.0.

## 1.2.0

- Reworked enchantment compatibility removal to patch loaded ENCH `wornRestrictions` data instead of relying on a version-sensitive validation-code signature.
- Kept exclusions and made excluded enchantments protect their resolved base-enchantment restrictions.
- Hardened the enchantment-slot scanner to recognize register and immediate writes to the menu slot-limit field.
- Added clearer runtime logging.
- Added one-click `Build-EnchantingFreedomAE.ps1` and `Build.cmd` build helpers.
- Updated packaging to 1.2.0 and added optional overwrite support for local rebuilds.
- Primary compatibility targets: Skyrim 1.6.1170 and 1.7.104.0.
