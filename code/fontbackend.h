/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstddef>
#include <vector>

/// <summary>
/// Reads the outline face a release prepared from a lettering source, naming
/// the source as the game holds it -- "DLGSYSI.PCX", "FULLFNT3.SHP". The bytes
/// are the face file itself, for a toolkit to take as it would a file's. False
/// when the release prepared none, which leaves the caller drawing from the
/// lettering the game shipped.
/// </summary>
bool Prepared_Face_Read(char const * source_name, std::vector<unsigned char> & bytes);
