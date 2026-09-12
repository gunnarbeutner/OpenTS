/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

// Reads a value of a key beneath HKEY_LOCAL_MACHINE into the buffer, which holds size bytes;
// the value is not terminated. False when the value cannot be read. The buffer is left alone
// when the key cannot be opened, and always on a target without a registry.
bool Platform_Read_Machine_Registry(char const * key, char const * value, void * buffer, unsigned int size);
