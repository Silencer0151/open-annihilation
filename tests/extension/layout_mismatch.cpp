// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A unit that sees Runtime with an extension's members, for a program whose
// layout marker (extension_members.cpp) is built without them: linking it
// must fail on the marker of the layout this unit sees (runtime.hpp).
#include "oa/app/runtime.hpp"

/// Returns at once; the program exists only to be linked.
///
/// @return 0
int main() {
    return 0;
}
