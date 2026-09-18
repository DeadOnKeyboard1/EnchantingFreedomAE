// Copyright (C) 2026 DeadOnKeyboard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Prevent Windows GDI from defining GetObject as GetObjectA/GetObjectW.
// CommonLib classes legitimately expose methods named GetObject().
#ifndef NOGDI
#  define NOGDI
#endif

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <xbyak/xbyak.h>

// Some Windows SDK include paths may still leak the GDI macro despite NOGDI.
// Never allow it to rewrite CommonLib member calls such as GetObject().
#ifdef GetObject
#  undef GetObject
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
