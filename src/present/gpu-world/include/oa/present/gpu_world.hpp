// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The world the graphics card draws: the card-ready forms of the game's
// assets, built on the processor once per map and never drawn here. Pure
// C++20 with no window, renderer or platform interface; the engine's
// presentation hands the results to the card.

#include "oa/present/gpu_world/sprite_pages.hpp"
#include "oa/present/gpu_world/terrain_atlas.hpp"
#include "oa/present/gpu_world/texel.hpp"
