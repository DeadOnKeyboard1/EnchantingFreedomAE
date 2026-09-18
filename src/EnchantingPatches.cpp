// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BUILD_SOURCE_ID: EF-AE-1.3.0-r13-staff-effects
// Parts of the enchanting-menu hook strategy are based on publicly documented
// GPL/MIT implementations listed in THIRD_PARTY_NOTICES.md.

#include "EnchantingPatches.h"



#include "Config.h"

namespace EnchantingFreedom
{
	namespace
	{
		using EnchantMenu = RE::CraftingSubMenus::EnchantConstructMenu;
		using FilterFlag = EnchantMenu::FilterFlag;

		constexpr REL::RelocationID kEnchantMenuConstructorID{ 50329, 51242 };
		constexpr REL::RelocationID kPopulateEntryListID{ 50454, 51359 };
		constexpr REL::ID kAddEnchantmentIfKnownID{ 51285 };

		constexpr std::size_t  kConstructorScanSize = 0x800;
		constexpr std::uint32_t kSelectionsLimitOffset = 0x198;

		// AE 1.6.1170 / 1.7.104.0. This is the block that classifies inventory
		// entries as EnchantArmor / EnchantWeapon / DisenchantArmor / DisenchantWeapon.
		constexpr std::ptrdiff_t kAEItemFilterOffset = 0x14D;
		constexpr std::size_t    kAEItemFilterReplaceSize = 0xD0;
		constexpr std::ptrdiff_t kAEItemFilterAcceptOffset = 0xD0;
		constexpr std::ptrdiff_t kAEItemFilterRejectOffset = 0x1C7;

		// EnchantConstructMenu::PopulateEntryList call to the quest-item test.
		constexpr std::ptrdiff_t kSEQuestCheckOffset = 0x133;
		constexpr std::ptrdiff_t kAEQuestCheckOffset = 0x140;

		// Verified directly against the user's SkyrimSE.exe 1.7.104.0:
		// SHA-256 846efccf0c1374d71f892907f46549560f2fcb0a75cb87a3eed438baa0f1402f
		constexpr std::ptrdiff_t kAE104StaffCheckOffset = 0x1A9;
		constexpr std::ptrdiff_t kAE104UnarmedCheckOffset = 0x1D3;
		constexpr std::ptrdiff_t kAE104AcceptOffset = 0x21D;
		constexpr std::ptrdiff_t kAE104KnownCheckOffset = 0x269;
		constexpr std::ptrdiff_t kAE104RejectOffset = 0x314;
		constexpr std::size_t    kKnownCheckSearchSize = 0x500;
		constexpr std::size_t    kStaffPatternSearchSize = 0x400;
		constexpr std::size_t    kStaffEffectPatternSearchSize = 0x280;
		constexpr std::ptrdiff_t kAE104StaffEffectCasting1Offset = 0x173;
		constexpr std::ptrdiff_t kAE104StaffEffectDelivery1Offset = 0x183;
		constexpr std::ptrdiff_t kAE104StaffEffectCasting2Offset = 0x1DB;
		constexpr std::ptrdiff_t kAE104StaffEffectDelivery2Offset = 0x1EC;
		constexpr std::uint64_t  kAE104ItemFilterFnv1a = 0x56D7AFBF59F88338ULL;

		// Resolved during hook installation so the assembly callback never needs
		// to perform an Address Library lookup while Skyrim is executing the menu.
		RE::TESObjectWEAP** g_unarmedWeaponSlot = nullptr;

		struct SlotWriteSite
		{
			std::uintptr_t            address;
			std::vector<std::uint8_t> instruction;
		};

		SlotWriteSite FindSlotWrite(std::uintptr_t a_function)
		{
			std::vector<SlotWriteSite> matches;
			for (std::size_t offset = 0; offset + 7 <= kConstructorScanSize; ++offset) {
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_function + offset);

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


		struct KnownDisenchantSite
		{
			std::uintptr_t              address;
			std::array<std::uint8_t, 6> instruction;
			std::uintptr_t              rejectAddress;
			std::ptrdiff_t              patternOffset;
		};

		KnownDisenchantSite FindKnownDisenchantSite(std::uintptr_t a_function)
		{
			// Exact 1.7.104 sequence:
			//   call [rax+0xB8]   ; TESForm::GetKnown()
			//   test al, al
			//   sete r12b         ; enabled = !known
			//   cmp edi, 7
			//   je reject
			constexpr std::array<std::uint8_t, 17> pattern{
				0xFF, 0x90, 0xB8, 0x00, 0x00, 0x00,
				0x84, 0xC0,
				0x41, 0x0F, 0x94, 0xC4,
				0x83, 0xFF, 0x07,
				0x0F, 0x84
			};

			std::vector<std::ptrdiff_t> matches;
			for (std::size_t offset = 0; offset + pattern.size() <= kKnownCheckSearchSize; ++offset) {
				if (std::memcmp(
						reinterpret_cast<const void*>(a_function + offset),
						pattern.data(),
						pattern.size()) == 0) {
					matches.push_back(static_cast<std::ptrdiff_t>(offset));
				}
			}

			if (matches.size() != 1) {
				throw std::runtime_error(std::format(
					"Expected one known-enchantment enable check, found {}",
					matches.size()));
			}

			const auto patternAddress = a_function + matches.front();

			// Replace only `test al,al ; sete r12b` (6 bytes). This means the
			// earlier vanilla path that disables entries for other reasons stays
			// untouched and can still jump directly past this hook.
			const auto site = patternAddress + 6;
			std::array<std::uint8_t, 6> original{};
			std::memcpy(original.data(), reinterpret_cast<const void*>(site), original.size());
			constexpr std::array<std::uint8_t, 6> expected{
				0x84, 0xC0, 0x41, 0x0F, 0x94, 0xC4
			};
			if (original != expected) {
				throw std::runtime_error("Known-enchantment test/sete sequence changed unexpectedly");
			}

			const auto branch = patternAddress + 15;
			const auto* branchBytes = reinterpret_cast<const std::uint8_t*>(branch);
			if (branchBytes[0] != 0x0F || branchBytes[1] != 0x84) {
				throw std::runtime_error("Known-enchantment reject branch changed unexpectedly");
			}

			std::int32_t relative{};
			std::memcpy(std::addressof(relative), branchBytes + 2, sizeof(relative));
			const auto reject = branch + 6 + static_cast<std::intptr_t>(relative);
			if (reject < a_function || reject >= a_function + kConstructorScanSize) {
				throw std::runtime_error("Known-enchantment reject branch leaves the expected function range");
			}

			return { site, original, reject, matches.front() };
		}

		std::uint64_t Fnv1a64(std::span<const std::uint8_t> a_bytes) noexcept
		{
			std::uint64_t value = 14695981039346656037ULL;
			for (const auto byte : a_bytes) {
				value ^= byte;
				value *= 1099511628211ULL;
			}
			return value;
		}

		struct StaffBranchSite
		{
			std::uintptr_t              address;
			std::array<std::uint8_t, 6> instruction;
			std::uintptr_t              rejectAddress;
			std::ptrdiff_t              patternOffset;
		};

		StaffBranchSite FindStaffBranchSite(std::uintptr_t a_function)
		{
			constexpr std::array<std::uint8_t, 9> pattern{
				0x80, 0xBB, 0x9D, 0x01, 0x00, 0x00, 0x08,
				0x0F, 0x84
			};

			std::vector<std::ptrdiff_t> matches;
			for (std::size_t offset = 0; offset + pattern.size() <= kStaffPatternSearchSize; ++offset) {
				if (std::memcmp(
						reinterpret_cast<const void*>(a_function + offset),
						pattern.data(),
						pattern.size()) == 0) {
					matches.push_back(static_cast<std::ptrdiff_t>(offset));
				}
			}

			if (matches.size() != 1) {
				throw std::runtime_error(std::format(
					"Expected one vanilla staff exclusion branch, found {}",
					matches.size()));
			}

			const auto branch = a_function + matches.front() + 7;
			std::array<std::uint8_t, 6> original{};
			std::memcpy(original.data(), reinterpret_cast<const void*>(branch), original.size());

			if (original[0] != 0x0F || original[1] != 0x84) {
				throw std::runtime_error("Vanilla staff exclusion branch changed unexpectedly");
			}

			std::int32_t relative{};
			std::memcpy(std::addressof(relative), original.data() + 2, sizeof(relative));
			const auto reject = branch + original.size() + static_cast<std::intptr_t>(relative);
			if (reject < a_function || reject >= a_function + kConstructorScanSize) {
				throw std::runtime_error("Staff rejection branch leaves the expected function range");
			}
			return { branch, original, reject, matches.front() };
		}

		bool VerifyAE104PopulateEntryLayout(
			std::uintptr_t a_function,
			std::uintptr_t a_unarmedWeaponSlot) noexcept
		{
			if (REL::Module::get().version() != REL::Version{ 1, 7, 104, 0 }) {
				return true;
			}

			const auto match = [a_function](std::ptrdiff_t a_offset, std::initializer_list<std::uint8_t> a_bytes) {
				return std::equal(
					a_bytes.begin(),
					a_bytes.end(),
					reinterpret_cast<const std::uint8_t*>(a_function + a_offset));
			};

			if (!match(kAEItemFilterOffset, { 0x48, 0x8B, 0xCB }) ||
			    !match(kAE104StaffCheckOffset, { 0x80, 0xBB, 0x9D, 0x01, 0x00, 0x00, 0x08 }) ||
			    !match(kAE104UnarmedCheckOffset, { 0x48, 0x3B, 0x1D }) ||
			    !match(kAE104AcceptOffset, { 0x48, 0x8B, 0xCE })) {
				return false;
			}

			const auto block = std::span{
				reinterpret_cast<const std::uint8_t*>(a_function + kAEItemFilterOffset),
				kAEItemFilterReplaceSize
			};
			if (Fnv1a64(block) != kAE104ItemFilterFnv1a) {
				return false;
			}

			const auto instruction = a_function + kAE104UnarmedCheckOffset;
			std::int32_t displacement{};
			std::memcpy(
				std::addressof(displacement),
				reinterpret_cast<const void*>(instruction + 3),
				sizeof(displacement));
			const auto target = instruction + 7 + static_cast<std::intptr_t>(displacement);

			return target == a_unarmedWeaponSlot;
		}


		struct StaffEffectCallSites
		{
			std::array<std::uintptr_t, 2> casting;
			std::array<std::uintptr_t, 2> delivery;
		};

		StaffEffectCallSites FindStaffEffectCallSites(std::uintptr_t a_function)
		{
			constexpr std::array<std::uint8_t, 6> castingPattern{
				0xFF, 0x90, 0xA8, 0x02, 0x00, 0x00
			};
			constexpr std::array<std::uint8_t, 6> deliveryPattern{
				0xFF, 0x90, 0xB8, 0x02, 0x00, 0x00
			};

			std::vector<std::uintptr_t> castingMatches;
			std::vector<std::uintptr_t> deliveryMatches;

			for (std::size_t offset = 0;
			     offset + castingPattern.size() <= kStaffEffectPatternSearchSize;
			     ++offset) {
				const auto address = a_function + offset;
				const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);

				if (std::memcmp(bytes, castingPattern.data(), castingPattern.size()) == 0) {
					castingMatches.push_back(address);
				}
				if (std::memcmp(bytes, deliveryPattern.data(), deliveryPattern.size()) == 0) {
					deliveryMatches.push_back(address);
				}
			}

			if (castingMatches.size() != 2 || deliveryMatches.size() != 2) {
				throw std::runtime_error(std::format(
					"Expected two GetCastingType and two GetDelivery calls in AddEnchantmentIfKnown, found {} / {}",
					castingMatches.size(),
					deliveryMatches.size()));
			}

			return {
				{ castingMatches[0], castingMatches[1] },
				{ deliveryMatches[0], deliveryMatches[1] }
			};
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
			std::uintptr_t                 a_source,
			std::uintptr_t                 a_destination,
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
			std::uint32_t                 a_limit,
			std::uintptr_t                 a_returnAddress,
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

		bool HasDisallowKeyword(RE::TESForm* a_form, RE::BGSKeyword* a_keyword) noexcept
		{
			if (!a_form || !a_keyword) {
				return false;
		}

			auto* keywordForm = skyrim_cast<RE::BGSKeywordForm*>(a_form);
			return keywordForm && keywordForm->HasKeyword(a_keyword);
		}

		bool SupportsAdvancedMenuHooks() noexcept
		{
			const auto version = REL::Module::get().version();
			return version == REL::Version{ 1, 6, 1170, 0 } ||
			       version == REL::Version{ 1, 7, 104, 0 };
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
			"Installing Enchanting Freedom hooks for Skyrim {}",
			REL::Module::get().version().string());

		if (settings.enableSlotLimit) {
			InstallSlotLimitHook();
		} else {
			SKSE::log::info("Enchantment slot-limit override is disabled");
		}

		if (settings.allowNonPlayableItems &&
		    REL::Module::get().version() == REL::Version{ 1, 7, 104, 0 }) {
			InstallItemFilterHook();
		} else {
			if (settings.allowNonPlayableItems) {
				SKSE::log::warn(
					"AllowNonPlayableItems is unavailable on this runtime; continuing with safer targeted hooks");
			}
			if (settings.allowStaves) {
				InstallStaffHook();
			}
		}

		if (settings.allowStaves) {
			InstallStaffEffectHook();
		}

		if (settings.allowDisenchantKnownEnchantments) {
			InstallKnownDisenchantHook();
		}

		if (settings.allowQuestItems) {
			InstallQuestItemHook();
		}
	}

	void EnchantingPatches::InstallSlotLimitHook()
	{
		const auto& settings = Config::GetSingleton().GetSettings();

		try {
			REL::Relocation<std::uintptr_t> constructor{ kEnchantMenuConstructorID };
			const auto site = FindSlotWrite(constructor.address());

			if (REL::Module::get().version() == REL::Version{ 1, 7, 104, 0 }) {
				const auto slotOffset = site.address - constructor.address();
				const std::array<std::uint8_t, 3> ctorAnchor{ 0x4D, 0x8D, 0xA6 };
				if (slotOffset != 0x243 ||
				    std::memcmp(
					    reinterpret_cast<const void*>(constructor.address() + 0x1B3),
					    ctorAnchor.data(),
					    ctorAnchor.size()) != 0) {
					throw std::runtime_error("Skyrim 1.7.104 constructor layout does not match the verified executable");
				}
			}

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
				"Restriction removal will still load.",
				REL::Module::get().version().string(),
				e.what());
		}
	}

	void EnchantingPatches::InstallStaffHook()
	{
		if (!REL::Module::IsAE() || !SupportsAdvancedMenuHooks()) {
			SKSE::log::warn(
				"Staff enchanting hook skipped on unsupported Skyrim runtime {}",
				REL::Module::get().version().string());
			return;
		}

		if (SKSE::GetPluginInfo("AmmoEnchanting")) {
			SKSE::log::warn(
				"AmmoEnchanting detected: staff menu expansion disabled to avoid a hook conflict. "
				"Record-level enchanting/disenchanting freedom remains active.");
			return;
		}

		try {
			REL::Relocation<std::uintptr_t> populateEntryList{ REL::ID(51359) };
			const auto site = FindStaffBranchSite(populateEntryList.address());

			if (REL::Module::get().version() == REL::Version{ 1, 7, 104, 0 }) {
				if (site.patternOffset != kAE104StaffCheckOffset ||
				    site.rejectAddress != populateEntryList.address() + kAE104RejectOffset) {
					throw std::runtime_error(std::format(
						"1.7.104 staff branch resolved to unexpected offsets +0x{:X} / reject +0x{:X}",
						site.patternOffset,
						site.rejectAddress - populateEntryList.address()));
				}
			}

			struct Patch : Xbyak::CodeGenerator
			{
				Patch(
					std::uintptr_t a_callback,
					std::uintptr_t a_continue,
					std::uintptr_t a_reject)
				{
					Xbyak::Label continuePath;

					// The jump into this stub preserves the flags from Skyrim's
					// `cmp animationType, kStaff`. Non-staves take the original
					// fall-through without calling C++.
					jne(continuePath);

					mov(rcx, rbx);  // TESObjectWEAP*
					mov(rax, a_callback);
					call(rax);
					test(al, al);
					jz(continuePath);

					mov(rax, a_reject);
					jmp(rax);

					L(continuePath);
					mov(rax, a_continue);
					jmp(rax);
				}
			};

			Patch patch{
				reinterpret_cast<std::uintptr_t>(&StaffExcludedThunk),
				site.address + site.instruction.size(),
				site.rejectAddress
			};
			patch.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* memory = static_cast<std::uint8_t*>(trampoline.allocate(patch.getSize()));
			if (!memory) {
				throw std::runtime_error("Failed to allocate trampoline for staff hook");
			}
			std::memcpy(memory, patch.getCode<const void*>(), patch.getSize());
			REX::W32::FlushInstructionCache(
				REX::W32::GetCurrentProcess(),
				memory,
				patch.getSize());

			WriteRelativeJump(
				site.address,
				reinterpret_cast<std::uintptr_t>(memory),
				site.instruction);

			SKSE::log::info(
				"Installed targeted staff hook at PopulateEntryList +0x{:X}; item exclusions remain respected",
				site.patternOffset + 7);
		} catch (const std::exception& e) {
			SKSE::log::error("Staff enchanting hook could not be installed: {}", e.what());
		}
	}

	void EnchantingPatches::InstallItemFilterHook()
	{
		// The broad classifier replaces 0xD0 bytes. Unlike the small signature
		// hooks, this path is enabled only for the supplied/verified 1.7.104 EXE.
		if (REL::Module::get().version() != REL::Version{ 1, 7, 104, 0 }) {
			SKSE::log::warn(
				"AllowNonPlayableItems requires the verified Skyrim 1.7.104 layout; "
				"the broad item-filter hook is skipped on runtime {}",
				REL::Module::get().version().string());
			return;
		}

		// Ammo Enchanting replaces this exact classification block. Hooks are
		// installed at kPostPostLoad, when SKSE::GetPluginInfo() is populated, so
		// detection is independent of DLL filename/load order.
		if (SKSE::GetPluginInfo("AmmoEnchanting")) {
			SKSE::log::warn(
				"AmmoEnchanting detected: staff/non-playable menu expansion disabled to avoid a hard hook conflict. "
				"Record-level enchanting/disenchanting freedom remains active.");
			return;
		}

		try {
			REL::Relocation<RE::TESObjectWEAP**> unarmedWeapon{ REL::ID(401061) };
			g_unarmedWeaponSlot = unarmedWeapon.get();
			if (!g_unarmedWeaponSlot) {
				throw std::runtime_error("Could not resolve Skyrim's internal unarmed weapon pointer");
			}

			REL::Relocation<std::uintptr_t> populateEntryList{ REL::ID(51359) };
			if (!VerifyAE104PopulateEntryLayout(
					populateEntryList.address(),
					reinterpret_cast<std::uintptr_t>(g_unarmedWeaponSlot))) {
				SKSE::log::error(
					"Item-filter hook not installed: Skyrim 1.7.104 verified layout check failed. "
					"No machine-code block was modified.");
				return;
			}

			const auto hook = populateEntryList.address() + kAEItemFilterOffset;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(hook);

			// This exact sequence is the beginning of the vanilla classification block.
			if (bytes[0] != 0x48 || bytes[1] != 0x8B || bytes[2] != 0xCB) {
				SKSE::log::error(
					"Item-filter hook not installed: expected vanilla bytes 48 8B CB at +0x{:X}. "
					"Another mod may already patch EnchantConstructMenu::PopulateEntryList.",
					kAEItemFilterOffset);
				return;
			}

			struct Patch : Xbyak::CodeGenerator
			{
				Patch(std::uintptr_t a_callback, std::uintptr_t a_accept, std::uintptr_t a_reject)
				{
					Xbyak::Label reject;

					// rsi is the InventoryEntryData* at this point in the vanilla function.
					mov(rcx, rsi);
					mov(rax, a_callback);
					call(rax);
					test(eax, eax);
					jz(reject);

					mov(edi, eax);
					mov(rax, a_accept);
					jmp(rax);

					L(reject);
					mov(rax, a_reject);
					jmp(rax);
				}
			};

			Patch patch{
				reinterpret_cast<std::uintptr_t>(&ClassifyItemThunk),
				hook + kAEItemFilterAcceptOffset,
				hook + kAEItemFilterRejectOffset
			};
			patch.ready();

			if (patch.getSize() > kAEItemFilterReplaceSize) {
				throw std::runtime_error("generated item-filter patch is too large");
			}

			std::vector<std::uint8_t> expected(
				reinterpret_cast<const std::uint8_t*>(hook),
				reinterpret_cast<const std::uint8_t*>(hook) + kAEItemFilterReplaceSize);
			std::vector<std::uint8_t> replacement(kAEItemFilterReplaceSize, REL::NOP);
			std::memcpy(replacement.data(), patch.getCode<const void*>(), patch.getSize());

			if (!REL::safe_write(
					hook,
					replacement.data(),
					replacement.size(),
					expected.data(),
					expected.size())) {
				throw std::runtime_error("Item-filter machine code changed before the verified block could be replaced");
			}

			SKSE::log::info(
				"Installed complete item-filter hook: staves={} nonPlayable={}",
				Config::GetSingleton().GetSettings().allowStaves,
				Config::GetSingleton().GetSettings().allowNonPlayableItems);
		} catch (const std::exception& e) {
			SKSE::log::error("Item-filter hook could not be installed: {}", e.what());
		}
	}



	void EnchantingPatches::InstallStaffEffectHook()
	{
		if (!REL::Module::IsAE() || !SupportsAdvancedMenuHooks()) {
			SKSE::log::warn(
				"Staff-effect list hook skipped on unsupported Skyrim runtime {}",
				REL::Module::get().version().string());
			return;
		}

		try {
			REL::Relocation<std::uintptr_t> addEnchantmentIfKnown{ kAddEnchantmentIfKnownID };
			const auto sites = FindStaffEffectCallSites(addEnchantmentIfKnown.address());

			if (REL::Module::get().version() == REL::Version{ 1, 7, 104, 0 }) {
				const std::array<std::ptrdiff_t, 2> castingOffsets{
					static_cast<std::ptrdiff_t>(sites.casting[0] - addEnchantmentIfKnown.address()),
					static_cast<std::ptrdiff_t>(sites.casting[1] - addEnchantmentIfKnown.address())
				};
				const std::array<std::ptrdiff_t, 2> deliveryOffsets{
					static_cast<std::ptrdiff_t>(sites.delivery[0] - addEnchantmentIfKnown.address()),
					static_cast<std::ptrdiff_t>(sites.delivery[1] - addEnchantmentIfKnown.address())
				};

				if (castingOffsets != std::array<std::ptrdiff_t, 2>{
						kAE104StaffEffectCasting1Offset,
						kAE104StaffEffectCasting2Offset } ||
				    deliveryOffsets != std::array<std::ptrdiff_t, 2>{
						kAE104StaffEffectDelivery1Offset,
						kAE104StaffEffectDelivery2Offset }) {
					throw std::runtime_error(std::format(
						"1.7.104 AddEnchantmentIfKnown layout changed: casting +0x{:X}/+0x{:X}, delivery +0x{:X}/+0x{:X}",
						castingOffsets[0],
						castingOffsets[1],
						deliveryOffsets[0],
						deliveryOffsets[1]));
				}
			}

			auto& trampoline = SKSE::GetTrampoline();

			for (const auto address : sites.casting) {
				trampoline.write_call<6>(address, &StaffEffectCastingTypeThunk);
			}
			for (const auto address : sites.delivery) {
				trampoline.write_call<6>(address, &StaffEffectDeliveryThunk);
			}

			SKSE::log::info(
				"Installed staff-effect list hook: StaffEnchantment records are categorized as weapon effects in the enchanting menu");
		} catch (const std::exception& e) {
			SKSE::log::error("Staff-effect list hook could not be installed: {}", e.what());
		}
	}

	void EnchantingPatches::InstallKnownDisenchantHook()
	{
		if (!REL::Module::IsAE() || !SupportsAdvancedMenuHooks()) {
			SKSE::log::warn(
				"Known-enchantment disenchant hook skipped on unsupported Skyrim runtime {}",
				REL::Module::get().version().string());
			return;
		}

		try {
			REL::Relocation<std::uintptr_t> populateEntryList{ REL::ID(51359) };
			const auto site = FindKnownDisenchantSite(populateEntryList.address());

			if (REL::Module::get().version() == REL::Version{ 1, 7, 104, 0 }) {
				if (site.patternOffset != kAE104KnownCheckOffset ||
				    site.rejectAddress != populateEntryList.address() + kAE104RejectOffset) {
					throw std::runtime_error(std::format(
						"1.7.104 known-enchantment check resolved to unexpected offsets +0x{:X} / reject +0x{:X}",
						site.patternOffset,
						site.rejectAddress - populateEntryList.address()));
				}
			}

			struct Patch : Xbyak::CodeGenerator
			{
				Patch(std::uintptr_t a_callback, std::uintptr_t a_continue)
				{
					// AL is TESForm::GetKnown(); RBX is the resolved base ENCH;
					// RSI is still the original InventoryEntryData*. Pass both the
					// base and the actual item entry so exact derived/runtime ENCH
					// exclusions cannot be lost when Skyrim resolves baseEnchantment.
					mov(r8, rsi);
					movzx(edx, al);
					mov(rcx, rbx);
					mov(rax, a_callback);
					call(rax);
					mov(r12b, al);

					mov(rax, a_continue);
					jmp(rax);
				}
			};

			Patch patch{
				reinterpret_cast<std::uintptr_t>(&KnownDisenchantThunk),
				site.address + site.instruction.size()
			};
			patch.ready();

			auto& trampoline = SKSE::GetTrampoline();
			auto* memory = static_cast<std::uint8_t*>(trampoline.allocate(patch.getSize()));
			if (!memory) {
				throw std::runtime_error("Failed to allocate trampoline for known-enchantment disenchant hook");
			}

			std::memcpy(memory, patch.getCode<const void*>(), patch.getSize());
			REX::W32::FlushInstructionCache(
				REX::W32::GetCurrentProcess(),
				memory,
				patch.getSize());

			WriteRelativeJump(
				site.address,
				reinterpret_cast<std::uintptr_t>(memory),
				site.instruction);

			SKSE::log::info(
				"Installed known-enchantment hook at PopulateEntryList +0x{:X}; only GetKnown() eligibility is overridden",
				site.patternOffset + 6);
		} catch (const std::exception& e) {
			SKSE::log::error("Known-enchantment disenchant hook could not be installed: {}", e.what());
		}
	}

	void EnchantingPatches::InstallQuestItemHook()
	{
		if (!REL::Module::IsAE() || !SupportsAdvancedMenuHooks()) {
			SKSE::log::warn(
				"Quest-item enchanting hook skipped on unsupported Skyrim runtime {}",
				REL::Module::get().version().string());
			return;
		}

		try {
			REL::Relocation<std::uintptr_t> questCheck{
				kPopulateEntryListID,
				REL::Relocate(kSEQuestCheckOffset, kAEQuestCheckOffset)
			};

			const auto opcode = *reinterpret_cast<const std::uint8_t*>(questCheck.address());
			if (opcode != 0xE8) {
				SKSE::log::error(
					"Quest-item hook not installed: expected CALL opcode at EnchantConstructMenu::PopulateEntryList +0x{:X}",
					REL::Module::IsAE() ? kAEQuestCheckOffset : kSEQuestCheckOffset);
				return;
			}

			auto& trampoline = SKSE::GetTrampoline();
			questCheckOriginal_ = trampoline.write_call<5>(questCheck.address(), QuestCheckThunk);
			SKSE::log::warn(
				"Quest items are enabled for the enchanting menu. Disenchanting a quest item destroys it and can break quests.");
		} catch (const std::exception& e) {
			SKSE::log::error("Quest-item hook could not be installed: {}", e.what());
		}
	}

	std::uint32_t EnchantingPatches::ClassifyItemThunk(RE::InventoryEntryData* a_entry)
	{
		return GetSingleton().ClassifyEnchantingItem(a_entry);
	}

	bool EnchantingPatches::StaffExcludedThunk(RE::TESObjectWEAP* a_weapon) noexcept
	{
		return a_weapon && GetSingleton().IsItemExcluded(a_weapon);
	}


	RE::MagicSystem::CastingType EnchantingPatches::StaffEffectCastingTypeThunk(
		RE::EnchantmentItem* a_enchantment) noexcept
	{
		if (!a_enchantment) {
			return RE::MagicSystem::CastingType::kConstantEffect;
		}

		if (!GetSingleton().IsEnchantmentExcluded(a_enchantment) &&
		    a_enchantment->data.spellType == RE::MagicSystem::SpellType::kStaffEnchantment) {
			return RE::MagicSystem::CastingType::kFireAndForget;
		}

		return a_enchantment->data.castingType;
	}

	RE::MagicSystem::Delivery EnchantingPatches::StaffEffectDeliveryThunk(
		RE::EnchantmentItem* a_enchantment) noexcept
	{
		if (!a_enchantment) {
			return RE::MagicSystem::Delivery::kSelf;
		}

		if (!GetSingleton().IsEnchantmentExcluded(a_enchantment) &&
		    a_enchantment->data.spellType == RE::MagicSystem::SpellType::kStaffEnchantment) {
			return RE::MagicSystem::Delivery::kTouch;
		}

		return a_enchantment->data.delivery;
	}

	bool EnchantingPatches::KnownDisenchantThunk(
		RE::EnchantmentItem* a_baseEnchantment,
		bool a_known,
		RE::InventoryEntryData* a_entry) noexcept
	{
		// Preserve vanilla behavior for unknown enchantments.
		if (!a_known) {
			return true;
		}

		// Skyrim has already resolved RBX to baseEnchantment before GetKnown().
		// Re-read the enchantment from the inventory entry so an exact derived
		// ENCH exclusion and a runtime-created FFxxxxxx ENCH remain distinguishable.
		auto* actualEnchantment = a_entry ? a_entry->GetEnchantment() : nullptr;
		auto* policyEnchantment = actualEnchantment ? actualEnchantment : a_baseEnchantment;

		if (!policyEnchantment ||
		    GetSingleton().IsEnchantmentExcluded(policyEnchantment)) {
			return false;
		}

		const auto& settings = Config::GetSingleton().GetSettings();
		if (!settings.allowDisenchantCreatedEnchantments &&
		    policyEnchantment->GetFormID() >= 0xFF000000) {
			return false;
		}

		return settings.allowDisenchantKnownEnchantments;
	}

	bool EnchantingPatches::QuestCheckThunk(RE::InventoryEntryData* a_entry)
	{
		const auto& settings = Config::GetSingleton().GetSettings();

		const bool originalResult = questCheckOriginal_.address() != 0 ?
			questCheckOriginal_(a_entry) :
			(a_entry && a_entry->IsQuestObject());

		// Bypass only the real quest-item condition. Favorited quest items remain
		// protected so the optional dangerous setting does not silently defeat
		// favorite-protection mods such as Essential Favorites.
		if (settings.allowQuestItems && a_entry && a_entry->GetObject() &&
		    a_entry->IsQuestObject() &&
		    !a_entry->IsFavorited() &&
		    !GetSingleton().IsItemExcluded(a_entry->GetObject())) {
			return false;
		}

		return originalResult;
	}

	std::uint32_t EnchantingPatches::ClassifyEnchantingItem(RE::InventoryEntryData* a_entry) const noexcept
	{
		if (!a_entry) {
			return 0;
		}

		auto* object = a_entry->GetObject();
		if (!object) {
			return 0;
		}

		const char* name = object->GetName();
		if (!name || name[0] == '\0') {
			return 0;
		}

		const auto& settings = Config::GetSingleton().GetSettings();
		const bool excluded = IsItemExcluded(object);
		const bool allowNonPlayable = settings.allowNonPlayableItems && !excluded;
		const bool allowStaves = settings.allowStaves && !excluded;

		if (!allowNonPlayable && !object->GetPlayable()) {
			return 0;
		}

		RE::BGSKeyword* disallowKeyword = nullptr;
		if (auto* defaultObjects = RE::BGSDefaultObjectManager::GetSingleton()) {
			auto* disallowForm = defaultObjects->GetObject(
				RE::DEFAULT_OBJECT::kKeywordDisallowEnchanting);
			if (disallowForm) {
				disallowKeyword = skyrim_cast<RE::BGSKeyword*>(disallowForm);
			}
		}

		if (auto* armor = skyrim_cast<RE::TESObjectARMO*>(object)) {
			if (HasDisallowKeyword(object, disallowKeyword)) {
				return 0;
			}

			return static_cast<std::uint32_t>(
				a_entry->GetEnchantment() ? FilterFlag::DisenchantArmor : FilterFlag::EnchantArmor);
		}

		if (auto* weapon = skyrim_cast<RE::TESObjectWEAP*>(object)) {
			// Preserve Skyrim's internal unarmed pseudo-weapon exclusion even when
			// AllowNonPlayableItems is enabled.
			if (g_unarmedWeaponSlot && weapon == *g_unarmedWeaponSlot) {
				return 0;
			}

			if (!allowStaves && weapon->IsStaff()) {
				return 0;
			}

			if (HasDisallowKeyword(object, disallowKeyword)) {
				return 0;
			}

			if (!allowNonPlayable &&
			    weapon->weaponData.flags.all(RE::TESObjectWEAP::Data::Flag::kNonPlayable)) {
				return 0;
			}

			return static_cast<std::uint32_t>(
				a_entry->GetEnchantment() ? FilterFlag::DisenchantWeapon : FilterFlag::EnchantWeapon);
		}

		if (skyrim_cast<RE::TESSoulGem*>(object)) {
			return a_entry->GetSoulLevel() == RE::SOUL_LEVEL::kNone ?
				0 :
				static_cast<std::uint32_t>(FilterFlag::SoulGem);
		}

		return 0;
	}

	void EnchantingPatches::RegisterMenuEvents()
	{
		const auto& settings = Config::GetSingleton().GetSettings();
		if (!settings.refreshOnCraftingMenuOpen || menuEventsRegistered_) {
			return;
		}

		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			SKSE::log::warn("UI is unavailable; crafting-menu refresh event was not registered");
			return;
		}

		ui->AddEventSink<RE::MenuOpenCloseEvent>(this);
		menuEventsRegistered_ = true;
		SKSE::log::info("Registered Crafting Menu refresh listener");
	}

	RE::BSEventNotifyControl EnchantingPatches::ProcessEvent(
		const RE::MenuOpenCloseEvent* a_event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
	{
		if (a_event && a_event->opening && a_event->menuName == RE::CraftingMenu::MENU_NAME) {
			try {
				ApplyDataPatches(false);
				SKSE::log::debug("Refreshed enchanting/disenchanting record patches before Crafting Menu use");
			} catch (const std::exception& e) {
				SKSE::log::error("Crafting Menu refresh failed: {}", e.what());
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void EnchantingPatches::ApplyDataPatches(bool a_logSummary)
	{
		ResolveExclusions();

		const auto& settings = Config::GetSingleton().GetSettings();
		auto* dataHandler = RE::TESDataHandler::GetSingleton();
		if (!dataHandler) {
			SKSE::log::error("TESDataHandler is not available; data patches were not applied");
			return;
		}

		std::size_t clearedWornRestrictions = 0;
		std::size_t alreadyUnrestricted = 0;
		std::size_t protectedEnchantments = 0;

		if (settings.removeEnchantingRestrictions) {
			for (auto* enchantment : dataHandler->GetFormArray<RE::EnchantmentItem>()) {
				if (!enchantment) {
					continue;
				}

				if (IsEnchantmentExcluded(enchantment)) {
					++protectedEnchantments;
					continue;
				}

				if (enchantment->data.wornRestrictions) {
					// Null means unrestricted in EnchantConstructMenu compatibility checks.
					enchantment->data.wornRestrictions = nullptr;
					++clearedWornRestrictions;
				} else {
					++alreadyUnrestricted;
				}
			}
		}

		std::size_t patchedArmor = 0;
		std::size_t patchedWeapons = 0;
		std::size_t patchedEnchantments = 0;

		if (settings.removeDisenchantingRestrictions) {
			auto* disallowKeyword = dataHandler->LookupForm<RE::BGSKeyword>(0xC27BD, "Skyrim.esm");
			if (!disallowKeyword) {
				SKSE::log::error("Could not resolve Skyrim.esm:000C27BD (MagicDisallowEnchanting)");
			} else {
				auto removeKeyword = [&](auto* a_form) {
					bool changed = false;
					while (a_form && a_form->RemoveKeyword(disallowKeyword)) {
						changed = true;
					}
					return changed;
				};

				for (auto* armor : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
					if (armor && !IsItemExcluded(armor) && removeKeyword(armor)) {
						++patchedArmor;
					}
				}

				for (auto* weapon : dataHandler->GetFormArray<RE::TESObjectWEAP>()) {
					if (weapon && !IsItemExcluded(weapon) && removeKeyword(weapon)) {
						++patchedWeapons;
					}
				}

				// Some unique/modded items are protected by putting the keyword on the
				// ENCH record itself rather than (or in addition to) the ARMO/WEAP.
				for (auto* enchantment : dataHandler->GetFormArray<RE::EnchantmentItem>()) {
					if (enchantment && !IsEnchantmentExcluded(enchantment) && removeKeyword(enchantment)) {
						++patchedEnchantments;
					}
				}
			}
		}

		if (a_logSummary) {
			SKSE::log::info(
				"Data patches complete: wornRestrictions cleared={} alreadyNull={} protectedENCH={} MagicDisallow removed from ARMO={} WEAP={} ENCH={}",
				clearedWornRestrictions,
				alreadyUnrestricted,
				protectedEnchantments,
				patchedArmor,
				patchedWeapons,
				patchedEnchantments);
		}
	}

	bool EnchantingPatches::IsEnchantmentExcluded(RE::EnchantmentItem* a_enchantment) const noexcept
	{
		if (!a_enchantment) {
			return false;
		}

		auto* base = ResolveBaseEnchantment(a_enchantment);
		// Exact derived exclusions protect only that exact ENCH. If the base ENCH
		// itself is explicitly excluded, descendants resolving to it are protected.
		if (excludedEnchantments_.contains(a_enchantment) ||
		    (base && excludedEnchantments_.contains(base))) {
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
				excludedEnchantments_.insert(enchantment);
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
	}
}
