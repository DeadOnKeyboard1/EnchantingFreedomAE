// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace EnchantingFreedom
{
	class EnchantingPatches final :
		public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static EnchantingPatches& GetSingleton();

		void InstallHooks();
		void ApplyDataPatches(bool a_logSummary = true);
		void RegisterMenuEvents();

	private:
		using QuestCheckFunction = bool(RE::InventoryEntryData*);

		RE::BSEventNotifyControl ProcessEvent(
			const RE::MenuOpenCloseEvent* a_event,
			RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_source) override;

		[[nodiscard]] bool IsEnchantmentExcluded(RE::EnchantmentItem* a_enchantment) const noexcept;
		[[nodiscard]] bool IsItemExcluded(RE::TESForm* a_item) const noexcept;
		[[nodiscard]] std::uint32_t ClassifyEnchantingItem(RE::InventoryEntryData* a_entry) const noexcept;

		void ResolveExclusions();
		void InstallSlotLimitHook();
		void InstallStaffHook();
		void InstallItemFilterHook();
		void InstallStaffEffectHook();
		void InstallKnownDisenchantHook();
		void InstallQuestItemHook();

		static std::uint32_t ClassifyItemThunk(RE::InventoryEntryData* a_entry);
		static bool StaffExcludedThunk(RE::TESObjectWEAP* a_weapon) noexcept;
		static RE::MagicSystem::CastingType StaffEffectCastingTypeThunk(
			RE::EnchantmentItem* a_enchantment) noexcept;
		static RE::MagicSystem::Delivery StaffEffectDeliveryThunk(
			RE::EnchantmentItem* a_enchantment) noexcept;
		static bool KnownDisenchantThunk(
			RE::EnchantmentItem* a_baseEnchantment,
			bool a_known,
			RE::InventoryEntryData* a_entry) noexcept;
		static bool QuestCheckThunk(RE::InventoryEntryData* a_entry);

		std::unordered_set<RE::EnchantmentItem*> excludedEnchantments_;
		std::unordered_set<RE::TESForm*>         excludedItems_;
		std::vector<std::string>                 excludedEnchantmentPlugins_;
		std::vector<std::string>                 excludedItemPlugins_;

		bool menuEventsRegistered_{ false };

		static inline REL::Relocation<QuestCheckFunction> questCheckOriginal_;
	};
}
