/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

// How a browser build finds its game data: assets.json names a hashed
// manifest listing every archive and film of an OpenTS-Assets release at its
// content-addressed path. Module.opentsManifestBase, set before the module
// loads, resolves both against another origin than the page.

#pragma once

#include "blocksource.h"

#if defined(OPENTS_WIN32_SUBSTITUTE)

#include <cstdint>
#include <memory>
#include <string>
#include <vector>


/// <summary>
/// Resolves a bare filename such as "TIBSUN.MIX" to its whole-file entry and
/// the volume holding it, or null when no manifest carries the name. The
/// volume is cached under the name, so a second open shares its source.
/// </summary>
std::shared_ptr<BlockFileClass> Manifest_Find(char const * name, BlockEntryClass & entry);

/// <summary>
/// Resolves a movie's filename to the URL the browser's video element fetches
/// it from, or an empty string when no manifest carries the movie.
/// </summary>
std::string Manifest_Find_Movie(char const * name);

// Whether a release marks a file as one to keep for offline play. A release that marks
// nothing -- one built before the field existed -- answers true for everything, which
// leaves the caller's own judgement in charge.
bool Manifest_Offline(char const * name);

/// <summary>
/// Returns every name the manifest's file list carries, for wildcard
/// searches; empty when there is no manifest.
/// </summary>
std::vector<std::string> Manifest_List_Files(void);

#endif
