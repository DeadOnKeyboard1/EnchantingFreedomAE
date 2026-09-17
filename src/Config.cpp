// Copyright (C) 2026 <COMPUTER_NAME>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"

namespace EnchantingFreedom
{
	namespace
	{
		constexpr std::uint32_t kMinimumEnchantments = 1;
		constexpr std::uint32_t kMaximumEnchantments = 50;
		constexpr std::uint32_t kUtf8CodePage = 65001;
		constexpr auto          kSettingsSection = L"EnchantingFreedom";
		constexpr auto          kExclusionsSection = L"Exclusions";

		std::filesystem::path BuildConfigPath()
		{
			auto executable = std::filesystem::path(REL::Module::get().filePath());
			return executable.parent_path() / "Data" / "SKSE" / "Plugins" / "EnchantingFreedomAE.ini";
		}

		std::optional<std::wstring> ReadString(
			const std::filesystem::path& a_path,
			const wchar_t*               a_section,
			const wchar_t*               a_key)
		{
			constexpr auto kMissing = L"{FCE47859-DB02-42CB-A917-872070E1095D}";
			std::array<wchar_t, 4096> buffer{};
			const auto length = REX::W32::GetPrivateProfileStringW(
				a_section,
				a_key,
				kMissing,
				buffer.data(),
				static_cast<std::uint32_t>(buffer.size()),
				a_path.c_str());

			std::wstring value(buffer.data(), length);
			if (value == kMissing) {
				return std::nullopt;
			}
			return value;
		}

		std::string ToUtf8(std::wstring_view a_value)
		{
			if (a_value.empty()) {
				return {};
			}

			const auto size = REX::W32::WideCharToMultiByte(
				kUtf8CodePage,
				0,
				a_value.data(),
				static_cast<std::int32_t>(a_value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (size <= 0) {
				throw std::runtime_error("failed to convert INI value to UTF-8");
			}

			std::string result(static_cast<std::size_t>(size), '\0');
			REX::W32::WideCharToMultiByte(
				kUtf8CodePage,
				0,
				a_value.data(),
				static_cast<std::int32_t>(a_value.size()),
				result.data(),
				size,
				nullptr,
				nullptr);
			return result;
		}

		std::string Trim(std::string_view a_value)
		{
			const auto first = std::ranges::find_if_not(a_value, [](unsigned char a_character) {
				return std::isspace(a_character) != 0;
			});
			const auto last = std::ranges::find_if_not(a_value | std::views::reverse, [](unsigned char a_character) {
				return std::isspace(a_character) != 0;
			}).base();
			return first < last ? std::string(first, last) : std::string{};
		}

		void ReadBool(
			const std::filesystem::path& a_path,
			const wchar_t*               a_section,
			const wchar_t*               a_key,
			bool&                        a_value)
		{
			const auto raw = ReadString(a_path, a_section, a_key);
			if (!raw) {
				return;
			}

			auto value = ToUtf8(*raw);
			std::ranges::transform(value, value.begin(), [](unsigned char a_character) {
				return static_cast<char>(std::tolower(a_character));
			});
			if (value == "true" || value == "1" || value == "yes" || value == "on") {
				a_value = true;
			} else if (value == "false" || value == "0" || value == "no" || value == "off") {
				a_value = false;
			} else {
				SKSE::log::warn("Ignoring invalid Boolean value '{}'", value);
			}
		}

		void ReadUnsigned(
			const std::filesystem::path& a_path,
			const wchar_t*               a_section,
			const wchar_t*               a_key,
			std::uint32_t&               a_value)
		{
			const auto raw = ReadString(a_path, a_section, a_key);
			if (!raw) {
				return;
			}

			const auto value = ToUtf8(*raw);
			std::uint32_t parsed{};
			const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
			if (error == std::errc{} && end == value.data() + value.size()) {
				a_value = parsed;
			} else {
				SKSE::log::warn("Ignoring invalid unsigned integer value '{}'", value);
			}
		}

		void ReadList(
			const std::filesystem::path& a_path,
			const wchar_t*               a_section,
			const wchar_t*               a_key,
			std::vector<std::string>&    a_values)
		{
			const auto raw = ReadString(a_path, a_section, a_key);
			if (!raw) {
				return;
			}

			const auto value = ToUtf8(*raw);
			std::size_t start = 0;
			while (start <= value.size()) {
				const auto end = value.find(',', start);
				auto entry = Trim(std::string_view(value).substr(start, end - start));
				if (!entry.empty()) {
					a_values.push_back(std::move(entry));
				}
				if (end == std::string::npos) {
					break;
				}
				start = end + 1;
			}
		}
	}

	Config& Config::GetSingleton()
	{
		static Config singleton;
		return singleton;
	}

	void Config::Load()
	{
		path_ = BuildConfigPath();
		settings_ = {};

		std::ifstream input(path_);
		if (!input) {
			SKSE::log::warn("Configuration not found at {}; using defaults", path_.string());
			return;
		}

		try {
			ReadBool(path_, kSettingsSection, L"EnableEnchantmentSlotLimit", settings_.enableSlotLimit);
			ReadUnsigned(path_, kSettingsSection, L"EnchantmentMaxSlots", settings_.maximumEnchantments);
			ReadBool(path_, kSettingsSection, L"RemoveEnchantingRestrictions", settings_.removeEnchantingRestrictions);
			ReadBool(path_, kSettingsSection, L"RemoveDisenchantingRestrictions", settings_.removeDisenchantingRestrictions);
			ReadList(path_, kExclusionsSection, L"Enchantments", settings_.excludedEnchantments);
			ReadList(path_, kExclusionsSection, L"Items", settings_.excludedItems);

			const auto requested = settings_.maximumEnchantments;
			settings_.maximumEnchantments = std::clamp(
				settings_.maximumEnchantments,
				kMinimumEnchantments,
				kMaximumEnchantments);
			if (requested != settings_.maximumEnchantments) {
				SKSE::log::warn(
					"EnchantmentMaxSlots {} was clamped to {}",
					requested,
					settings_.maximumEnchantments);
			}
		} catch (const std::exception& e) {
			settings_ = {};
			SKSE::log::error("Failed to read {}: {}; using defaults", path_.string(), e.what());
		}

		SKSE::log::info(
			"Configuration: slots={} max={} enchantingRestrictions={} disenchantingRestrictions={}",
			settings_.enableSlotLimit,
			settings_.maximumEnchantments,
			settings_.removeEnchantingRestrictions,
			settings_.removeDisenchantingRestrictions);
	}

	const Settings& Config::GetSettings() const noexcept
	{
		return settings_;
	}

	const std::filesystem::path& Config::GetPath() const noexcept
	{
		return path_;
	}
}
