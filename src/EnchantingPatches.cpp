// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EnchantingPatches.h"

#include "Config.h"

namespace EnchantingFreedom
{
	namespace
	{
		// Address Library resolves this constructor for the installed runtime.
		// The strict instruction matcher below is intentionally kept identical to
		// the v1.1.0 approach that was known to work on Skyrim 1.7.104.0.
		constexpr REL::RelocationID kEnchantMenuConstructorID{ 50329, 51242 };
		constexpr std::size_t       kConstructorScanSize = 0x800;
		constexpr std::uint32_t      kSelectionsLimitOffset = 0x198;

		struct SlotWriteSite
		{
			std::uintptr_t          address;
			std::vector<std::uint8_t> instruction;
		};

		SlotWriteSite FindSlotWrite(std::uintptr_t a_function)
		{
			std::vector<SlotWriteSite> matches;
			for (std::size_t offset = 0; offset + 7 <= kConstructorScanSize; ++offset) {
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_function + offset);

				// Expected compiler form:
				//   REX 89 /r disp32
				// with a non-SIB base and a dword destination at +0x198.
				// Keeping this deliberately strict avoids false positives while scanning
				// arbitrary instruction bytes inside the constructor.
				if ((bytes[0] & 0xF0) != 0x40 || bytes[1] != 0x89 ||
				    (bytes[2] & 0xC0) != 0x80 || (bytes[2] & 0x38) != 0 ||
				    (bytes[2] & 0x07) == 0x04) {
					continue;
				}

				std::uint32_t displacement{};
				std::memcpy(std::addressof(displacement), bytes + 3, sizeof(displacement));
				if (displacement == kSelectionsLimitOffset) {
					matches.push_back({ a_function + offset, { bytes, bytes + 7 } });
				}
			}

			if (matches.size() != 1) {
				throw std::runtime_error(std::format(
					"Expected one enchantment-slot write in Skyrim {}, found {}",
					REL::Module::get().version().string(),
					matches.size()));
			}
			return std::move(matches.front());
		}

		std::string Trim(std::string_view a_value)
		{
			const auto first = a_value.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_value.find_last_not_of(" \t\r\n");
			return std::string(a_value.substr(first, last - first + 1));
		}

		std::string ToLower(std::string_view a_value)
		{
			std::string result(a_value);
			std::ranges::transform(result, result.begin(), [](unsigned char a_character) {
				return static_cast<char>(std::tolower(a_character));
			});
			return result;
		}

		struct FormSpec
		{
			std::string              plugin;
			std::optional<RE::FormID> localFormID;
			bool                     wildcard{ false };
		};

		std::optional<FormSpec> ParseFormSpec(std::string_view a_text)
		{
			const auto separator = a_text.find('|');
			if (separator == std::string_view::npos) {
				return std::nullopt;
			}

			auto formText = Trim(a_text.substr(0, separator));
			auto plugin = Trim(a_text.substr(separator + 1));
			if (formText.empty() || plugin.empty()) {
				return std::nullopt;
			}

			if (formText == "*") {
				return FormSpec{ std::move(plugin), std::nullopt, true };
			}

			if (formText.starts_with("0x") || formText.starts_with("0X")) {
				formText.erase(0, 2);
			}

			RE::FormID value = 0;
			const auto [end, error] = std::from_chars(
				formText.data(),
				formText.data() + formText.size(),
				value,
				16);
			if (error != std::errc{} || end != formText.data() + formText.size()) {
				return std::nullopt;
			}

			return FormSpec{ std::move(plugin), value, false };
		}

		RE::EnchantmentItem* ResolveBaseEnchantment(RE::EnchantmentItem* a_enchantment) noexcept
		{
			for (std::uint32_t depth = 0; a_enchantment && depth < 64; ++depth) {
				auto* next = a_enchantment->data.baseEnchantment;
				if (!next || next == a_enchantment) {
					break;
				}
				a_enchantment = next;
			}
			return a_enchantment;
		}

		bool HasOriginPlugin(const RE::TESForm* a_form, const std::vector<std::string>& a_plugins) noexcept
		{
			if (const auto* file = a_form ? a_form->GetFile(0) : nullptr) {
				const auto filename = file->GetFilename();
				return std::ranges::any_of(a_plugins, [&](const std::string& a_plugin) {
					return std::ranges::equal(filename, a_plugin, [](unsigned char a_left, unsigned char a_right) {
						return std::tolower(a_left) == std::tolower(a_right);
					});
				});
			}
			return false;
		}

		class CodeBuffer
		{
		public:
			void Byte(std::uint8_t a_value) { bytes_.push_back(a_value); }

			void DWord(std::uint32_t a_value)
			{
				const auto* begin = reinterpret_cast<const std::uint8_t*>(std::addressof(a_value));
				bytes_.insert(bytes_.end(), begin, begin + sizeof(a_value));
			}

			void QWord(std::uintptr_t a_value)
			{
				const auto* begin = reinterpret_cast<const std::uint8_t*>(std::addressof(a_value));
				bytes_.insert(bytes_.end(), begin, begin + sizeof(a_value));
			}

			void AbsoluteJump(std::uintptr_t a_destination)
			{
				Byte(0xFF);
				Byte(0x25);
				DWord(0);
				QWord(a_destination);
			}

			[[nodiscard]] const std::vector<std::uint8_t>& Bytes() const noexcept { return bytes_; }

		private:
			std::vector<std::uint8_t> bytes_;
		};

		std::uintptr_t AllocateCode(const CodeBuffer& a_code)
		{
			auto& trampoline = SKSE::GetTrampoline();
			auto* memory = static_cast<std::uint8_t*>(trampoline.allocate(a_code.Bytes().size()));
			if (!memory) {
				throw std::runtime_error("Failed to allocate SKSE trampoline memory");
			}
			std::memcpy(memory, a_code.Bytes().data(), a_code.Bytes().size());
			REX::W32::FlushInstructionCache(
				REX::W32::GetCurrentProcess(),
				memory,
				a_code.Bytes().size());
			return reinterpret_cast<std::uintptr_t>(memory);
		}

		void WriteRelativeJump(
			std::uintptr_t                a_source,
			std::uintptr_t                a_destination,
			std::span<const std::uint8_t> a_expected)
		{
			if (a_expected.size() < 5) {
				throw std::runtime_error("Hook instruction is too short for a relative jump");
			}

			const auto displacement =
				static_cast<std::int64_t>(a_destination) - static_cast<std::int64_t>(a_source + 5);
			if (displacement < (std::numeric_limits<std::int32_t>::min)() ||
			    displacement > (std::numeric_limits<std::int32_t>::max)()) {
				throw std::runtime_error("Trampoline branch is outside rel32 range");
			}

			std::vector<std::uint8_t> patch(a_expected.size(), 0x90);
			patch[0] = 0xE9;
			const auto relative = static_cast<std::int32_t>(displacement);
			std::memcpy(patch.data() + 1, std::addressof(relative), sizeof(relative));
			if (!REL::safe_write(
					a_source,
					patch.data(),
					patch.size(),
					a_expected.data(),
					a_expected.size())) {
				throw std::runtime_error(std::format("Unexpected machine code at 0x{:X}", a_source));
			}
		}

		std::uintptr_t BuildSlotStub(
			std::uint32_t                a_limit,
			std::uintptr_t               a_returnAddress,
			std::span<const std::uint8_t> a_originalInstruction)
		{
			if (a_originalInstruction.size() != 7) {
				throw std::runtime_error("Unsupported enchantment-slot instruction length");
			}

			CodeBuffer code;
			code.Byte(a_originalInstruction[0]);
			code.Byte(0xC7);
			code.Byte(static_cast<std::uint8_t>(a_originalInstruction[2] & 0xC7));

			std::uint32_t displacement{};
			std::memcpy(
				std::addressof(displacement),
				a_originalInstruction.data() + 3,
				sizeof(displacement));
			code.DWord(displacement);
			code.DWord(a_limit);
			code.AbsoluteJump(a_returnAddress);
			return AllocateCode(code);
		}
	}

	EnchantingPatches& EnchantingPatches::GetSingleton()
	{
		static EnchantingPatches singleton;
		return singleton;
	}

	void EnchantingPatches::InstallHooks()
	{
		const auto& settings = Config::GetSingleton().GetSettings();
		SKSE::log::info(
			"Installing runtime hooks for Skyrim {}",
			REL::Module::get().version().string());

		if (!settings.enableSlotLimit) {
			SKSE::log::info("Enchantment slot-limit override is disabled");
			return;
		}

		// The slot-limit feature is optional from the loader's point of view.
		// A pattern mismatch must never prevent Skyrim from starting.
		try {
			REL::Relocation<std::uintptr_t> constructor{ kEnchantMenuConstructorID };
			const auto site = FindSlotWrite(constructor.address());
			const auto stub = BuildSlotStub(
				settings.maximumEnchantments,
				site.address + site.instruction.size(),
				site.instruction);
			WriteRelativeJump(site.address, stub, site.instruction);
			SKSE::log::info(
				"Installed slot-limit hook at +0x{:X}: {} enchantments",
				site.address - constructor.address(),
				settings.maximumEnchantments);
		} catch (const std::exception& e) {
			SKSE::log::error(
				"Slot-limit hook could not be installed on Skyrim {}: {}. "
				"Enchanting restriction removal will still load.",
				REL::Module::get().version().string(),
				e.what());
		}
	}

	RE::BGSListForm* EnchantingPatches::BuildUniversalWornRestrictions()
	{
		if (universalWornRestrictions_) {
			return universalWornRestrictions_;
		}

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			SKSE::log::error("TESDataHandler is unavailable while building the universal worn-restrictions list");
			return nullptr;
		}

		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSListForm>();
		if (!factory) {
			SKSE::log::error("BGSListForm factory is unavailable");
			return nullptr;
		}

		auto* list = factory->Create();
		if (!list) {
			SKSE::log::error("Failed to create the universal worn-restrictions FormList");
			return nullptr;
		}

		std::size_t keywordCount = 0;
		for (auto* keyword : dataHandler->GetFormArray<RE::BGSKeyword>()) {
			if (!keyword) {
				continue;
			}
			list->forms.push_back(keyword);
			++keywordCount;
		}

		if (keywordCount == 0) {
			SKSE::log::error("No loaded keywords were found; universal enchanting restrictions were not created");
			return nullptr;
		}

		universalWornRestrictions_ = list;
		SKSE::log::info(
			"Created universal worn-restrictions FormList with {} loaded keywords",
			keywordCount);
		return universalWornRestrictions_;
	}

	void EnchantingPatches::ApplyDataPatches()
	{
		ResolveExclusions();

		const auto& settings = Config::GetSingleton().GetSettings();
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			SKSE::log::error("TESDataHandler is not available; data patches were not applied");
			return;
		}

		if (settings.removeEnchantingRestrictions) {
			auto* universalRestrictions = BuildUniversalWornRestrictions();
			if (universalRestrictions) {
				std::size_t patchedEnchantments = 0;
				std::size_t protectedEnchantments = 0;

				for (auto* enchantment : dataHandler->GetFormArray<RE::EnchantmentItem>()) {
					if (!enchantment) {
						continue;
					}

					if (IsEnchantmentExcluded(enchantment)) {
						++protectedEnchantments;
						continue;
					}

					if (enchantment->data.wornRestrictions != universalRestrictions) {
						enchantment->data.wornRestrictions = universalRestrictions;
						++patchedEnchantments;
					}
				}

				SKSE::log::info(
					"Assigned universal worn restrictions to {} loaded enchantments ({} protected by exclusions)",
					patchedEnchantments,
					protectedEnchantments);
			}
		}

		if (settings.removeDisenchantingRestrictions) {
			auto* disallowKeyword = dataHandler->LookupForm<RE::BGSKeyword>(0xC27BD, "Skyrim.esm");
			if (!disallowKeyword) {
				SKSE::log::error("Could not resolve Skyrim.esm:000C27BD (MagicDisallowEnchanting)");
			} else {
				std::size_t patchedItems = 0;
				auto patchItems = [&](auto& a_forms) {
					for (auto* form : a_forms) {
						if (!form || IsItemExcluded(form)) {
							continue;
						}

						bool changed = false;
						while (form->RemoveKeyword(disallowKeyword)) {
							changed = true;
						}
						if (changed) {
							++patchedItems;
						}
					}
				};

				patchItems(dataHandler->GetFormArray<RE::TESObjectARMO>());
				patchItems(dataHandler->GetFormArray<RE::TESObjectWEAP>());
				SKSE::log::info(
					"Removed MagicDisallowEnchanting from {} loaded item records",
					patchedItems);
			}
		}

		SKSE::log::info(
			"Data patches complete for Skyrim {}",
			REL::Module::get().version().string());
	}

	bool EnchantingPatches::IsEnchantmentExcluded(RE::EnchantmentItem* a_enchantment) const noexcept
	{
		if (!a_enchantment) {
			return false;
		}

		auto* base = ResolveBaseEnchantment(a_enchantment);
		if (excludedEnchantments_.contains(base)) {
			return true;
		}

		return HasOriginPlugin(a_enchantment, excludedEnchantmentPlugins_) ||
		       HasOriginPlugin(base, excludedEnchantmentPlugins_);
	}

	bool EnchantingPatches::IsItemExcluded(RE::TESForm* a_item) const noexcept
	{
		return a_item && (excludedItems_.contains(a_item) || HasOriginPlugin(a_item, excludedItemPlugins_));
	}

	void EnchantingPatches::ResolveExclusions()
	{
		excludedEnchantments_.clear();
		excludedItems_.clear();
		excludedEnchantmentPlugins_.clear();
		excludedItemPlugins_.clear();

		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			SKSE::log::error("TESDataHandler is not available while resolving exclusions");
			return;
		}

		const auto& settings = Config::GetSingleton().GetSettings();

		for (const auto& text : settings.excludedEnchantments) {
			const auto spec = ParseFormSpec(text);
			if (!spec) {
				SKSE::log::warn("Invalid excluded enchantment spec: {}", text);
				continue;
			}

			if (spec->wildcard) {
				excludedEnchantmentPlugins_.push_back(ToLower(spec->plugin));
				continue;
			}

			if (auto* enchantment = dataHandler->LookupForm<RE::EnchantmentItem>(
					*spec->localFormID,
					spec->plugin)) {
				excludedEnchantments_.insert(ResolveBaseEnchantment(enchantment));
			} else {
				SKSE::log::warn("Excluded enchantment was not found: {}", text);
			}
		}

		for (const auto& text : settings.excludedItems) {
			const auto spec = ParseFormSpec(text);
			if (!spec) {
				SKSE::log::warn("Invalid excluded item spec: {}", text);
				continue;
			}

			if (spec->wildcard) {
				excludedItemPlugins_.push_back(ToLower(spec->plugin));
				continue;
			}

			if (auto* item = dataHandler->LookupForm(*spec->localFormID, spec->plugin)) {
				excludedItems_.insert(item);
			} else {
				SKSE::log::warn("Excluded item was not found: {}", text);
			}
		}

		SKSE::log::info(
			"Resolved exclusions: {} enchantment forms, {} enchantment plugins, {} item forms, {} item plugins",
			excludedEnchantments_.size(),
			excludedEnchantmentPlugins_.size(),
			excludedItems_.size(),
			excludedItemPlugins_.size());
	}
}
