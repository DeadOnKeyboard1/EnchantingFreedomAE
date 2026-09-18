// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace EnchantingFreedom
{
	struct Settings
	{
		bool                     enableSlotLimit{ true };
		std::uint32_t            maximumEnchantments{ 50 };
		bool                     removeEnchantingRestrictions{ true };
		bool                     removeDisenchantingRestrictions{ true };
		bool                     allowDisenchantKnownEnchantments{ true };
		bool                     allowDisenchantCreatedEnchantments{ false };
		bool                     allowStaves{ true };
		bool                     allowNonPlayableItems{ false };
		bool                     allowQuestItems{ false };
		bool                     refreshOnCraftingMenuOpen{ true };
		std::vector<std::string> excludedEnchantments;
		std::vector<std::string> excludedItems;
	};

	class Config final
	{
	public:
		static Config& GetSingleton();

		void Load();

		[[nodiscard]] const Settings& GetSettings() const noexcept;
		[[nodiscard]] const std::filesystem::path& GetPath() const noexcept;

	private:
		Settings              settings_;
		std::filesystem::path path_;
	};
}
