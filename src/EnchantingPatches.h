// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace EnchantingFreedom
{
	class EnchantingPatches final
	{
	public:
		static EnchantingPatches& GetSingleton();

		void InstallHooks();
		void ApplyDataPatches();

	private:
		[[nodiscard]] bool IsEnchantmentExcluded(RE::EnchantmentItem* a_enchantment) const noexcept;
		[[nodiscard]] bool IsItemExcluded(RE::TESForm* a_item) const noexcept;

		void ResolveExclusions();
		RE::BGSListForm* BuildUniversalWornRestrictions();

		std::unordered_set<RE::EnchantmentItem*> excludedEnchantments_;
		std::unordered_set<RE::TESForm*>         excludedItems_;
		std::vector<std::string>                 excludedEnchantmentPlugins_;
		std::vector<std::string>                 excludedItemPlugins_;
		RE::BGSListForm*                         universalWornRestrictions_{ nullptr };
	};
}
