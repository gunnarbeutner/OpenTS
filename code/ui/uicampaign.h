/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <vector>


// Offers the campaigns listed by their Campaigns index. Accepting sets chosen and
// Options.Difficulty; false means the screen could not be prepared.
bool UI_Campaign_Screen(std::vector<int> const & campaigns, int & chosen);
