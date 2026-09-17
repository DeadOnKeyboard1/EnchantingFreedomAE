// Copyright (C) 2026 <COMPUTER_NAME>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Config.h"
#include "EnchantingPatches.h"

SKSEPluginInfo(
	.Version = REL::Version{ 1, 2, 1, 0 },
	.Name = "EnchantingFreedomAE",
	.Author = "<COMPUTER_NAME>",
	.StructCompatibility = SKSE::StructCompatibility::Independent,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
	.MinimumSKSEVersion = REL::Version{ 2, 0, 20, 0 }
)

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (a_message && a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			try {
				EnchantingFreedom::EnchantingPatches::GetSingleton().ApplyDataPatches();
			} catch (const std::exception& e) {
				SKSE::log::critical("Data patching failed: {}", e.what());
			}
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 512 });

	try {
		SKSE::log::info(
			"EnchantingFreedomAE 1.2.1 loading on Skyrim {}",
			REL::Module::get().version().string());

		EnchantingFreedom::Config::GetSingleton().Load();
		EnchantingFreedom::EnchantingPatches::GetSingleton().InstallHooks();

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
