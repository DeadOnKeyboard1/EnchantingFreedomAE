// Copyright (C) 2026 <COMPUTER_NAME>
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
