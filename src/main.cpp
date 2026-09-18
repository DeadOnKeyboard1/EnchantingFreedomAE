// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"
#include "EnchantingPatches.h"

// Export PluginVersionData directly. Skyrim 1.7.104 / SKSE 2.3.1 uses
// Address Library format 5 and reads the v5 capability bit from
// versionIndependenceEx. Both supported runtimes are post-1.6.629, so this
// plugin advertises the updated gameplay-structure layout instead of claiming
// that it uses no game structures.
static_assert(
	(SKSE::PluginVersionData{}.versionIndependenceEx &
	 SKSE::PluginVersionData::kVersionIndependentEx_AddressLibraryV5) != 0,
	"CommonLibSSE-NG must expose Address Library v5 plugin metadata");

SKSE_PLUGIN_VERSION = []() {
	SKSE::PluginVersionData version{};
	version.PluginVersion(REL::Version{ 1, 3, 0, 0 });
	version.PluginName("EnchantingFreedomAE");
	version.AuthorName("DeadOnKeyboard");
	version.UsesAddressLibrary();
	version.UsesUpdatedStructs();
	version.MinimumRequiredXSEVersion(REL::Version{ 2, 0, 20, 0 });
	return version;
}();

SKSE_EXPORT bool SKSEPlugin_Query(SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
	if (!a_info) {
		return false;
	}

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = SKSEPlugin_Version.GetPluginName().data();
	a_info->version = SKSEPlugin_Version.pluginVersion;
	return true;
}

namespace
{
	bool g_hookInstallationAttempted = false;

	void EnsureHooksInstalled()
	{
		if (g_hookInstallationAttempted) {
			return;
		}
		g_hookInstallationAttempted = true;

		try {
			EnchantingFreedom::EnchantingPatches::GetSingleton().InstallHooks();
		} catch (const std::exception& e) {
			// Do not retry after a partial installation; double-patching is more
			// dangerous than continuing with the hooks that were installed safely.
			SKSE::log::critical("Hook installation failed: {}", e.what());
		}
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		switch (a_message->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			// GetPluginInfo() is reliable here: SKSE has finished loading all DLLs
			// and has populated their public PluginInfo entries.
			EnsureHooksInstalled();
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			// Defensive fallback in case a nonstandard SKSE flow omitted PostPostLoad.
			EnsureHooksInstalled();
			try {
				auto& patches = EnchantingFreedom::EnchantingPatches::GetSingleton();
				patches.ApplyDataPatches();
				patches.RegisterMenuEvents();
			} catch (const std::exception& e) {
				SKSE::log::critical("Data patching failed: {}", e.what());
			}
			break;

		default:
			break;
		}
	}
}

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	// This release has machine-code hooks verified only for these two runtimes.
	const auto runtime = a_skse ? a_skse->RuntimeVersion() : REL::Version{};
	if (runtime != REL::Version{ 1, 6, 1170, 0 } &&
	    runtime != REL::Version{ 1, 7, 104, 0 }) {
		return false;
	}

	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 4096 });

	try {
		SKSE::log::info(
			"EnchantingFreedomAE 1.3.0 loading on Skyrim {}",
			REL::Module::get().version().string());

		EnchantingFreedom::Config::GetSingleton().Load();

		auto* messaging = SKSE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
			SKSE::log::critical("Failed to register the SKSE messaging listener");
			return false;
		}
	} catch (const std::exception& e) {
		SKSE::log::critical("Initialization failed: {}", e.what());
		return false;
	}

	SKSE::log::info(
		"Initialization complete for Skyrim {} ({})",
		REL::Module::get().version().string(),
		REL::Module::IsSE() ? "SE" : "AE");
	return true;
}
