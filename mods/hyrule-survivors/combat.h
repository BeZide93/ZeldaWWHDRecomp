/* SPDX-License-Identifier: MPL-2.0 */
#ifndef HS_COMBAT_H
#define HS_COMBAT_H
#include "core.h"
void hs_combat_reset(void);
void hs_combat_provision(void);
void hs_combat_step(void *link);
void hs_combat_stop(void *link);
int hs_combat_attack(int weapon, HsId target, int damage, float reach);
#endif
