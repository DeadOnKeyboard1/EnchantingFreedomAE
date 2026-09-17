# Enchanting Freedom AE 1.2.1

Enchanting Freedom AE is an independent clean-room SKSE plugin that removes Skyrim's enchanting and disenchanting restrictions.

## Runtime targets

The same DLL is intended for both of the primary AE targets below when the matching SKSE and Address Library are installed:

- Skyrim SE/AE 1.6.1170
- Skyrim SE/AE 1.7.104.0

The project remains Address-Library based and also keeps the SE/AE build configuration enabled in CommonLibSSE-NG. Skyrim VR is not supported.

## What changed in 1.2.1

The item/enchantment compatibility bypass no longer depends on a runtime-specific machine-code signature. After game data is loaded, the plugin creates an in-memory FormList containing all loaded keywords and assigns that list as `wornRestrictions` to every non-excluded enchantment. This also applies to enchantments supplied by other plugins.

The configurable enchantment-slot override still operates in the enchanting-menu constructor. v1.2.1 restores the strict, previously working instruction matcher instead of the overly broad scanner introduced in v1.2.0. A slot-hook mismatch is logged and no longer aborts SKSE plugin loading, so Skyrim can still start.

## Features

- Configurable maximum enchantment slots from 1 through 50
- Runtime-independent replacement of ENCH worn restrictions with a universal loaded-keyword FormList
- Optional removal of `MagicDisallowEnchanting` from loaded armor and weapon records
- Per-form and per-plugin exclusions
- Automatic SE/AE runtime address resolution through Address Library
- Plain INI configuration
- One-click PowerShell build and packaging script

## Requirements

Runtime:

- SKSE matching the installed Skyrim runtime
- Address Library for SKSE Plugins matching the installed runtime

Build:

- Windows x64
- Visual Studio / Build Tools with Desktop development with C++
- CMake 3.25+
- Ninja
- vcpkg
- Git/internet only when the pinned CommonLibSSE-NG source is not already available locally

No ESP, ESL, or original mod is required.

## One-click build

Double-click `Build.cmd`, or run:

```powershell
.\Build-EnchantingFreedomAE.ps1 -Clean
```

The script automatically checks common locations for:

- `..\_toolchain\vcpkg`
- `..\_toolchain\CommonLibSSE-NG`
- `..\_toolchain\CommonLibSSE`
- `..\_toolchain\CommonLibVR`
- `%VCPKG_ROOT%`

Custom paths can be supplied explicitly:

```powershell
.\Build-EnchantingFreedomAE.ps1 `
  -VcpkgRoot "C:\path\to\vcpkg" `
  -CommonLibSSEPath "C:\path\to\CommonLibSSE-NG" `
  -Clean
```

A successful normal build creates:

- `build-ninja\EnchantingFreedomAE.dll`
- `build-ninja\EnchantingFreedomAE.pdb`
- `dist\EnchantingFreedomAE-1.2.1-SE-AE-Universal.zip`

The release ZIP contains the SKSE plugin, PDB, default INI, GPL text, and copyright notice.

## Configuration

Settings are read from:

`Data/SKSE/Plugins/EnchantingFreedomAE.ini`

See `CONFIGURATION.md` and `config/EnchantingFreedomAE.ini`.

## Compatibility notes

Because the restriction removal works on loaded enchantment records, enchantments from other plugins are handled automatically unless excluded in the INI. Mods that overhaul the same records can still conflict with each other independently of Enchanting Freedom AE.

## License

Copyright (C) 2026 <COMPUTER_NAME>

Enchanting Freedom AE is licensed under GNU GPL version 3 or, at your option, any later version. See `LICENSE` and `NOTICE.md`.
