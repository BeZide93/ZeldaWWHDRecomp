// SPDX-License-Identifier: MPL-2.0
#pragma once
namespace enhanced::affinity {
enum class Role { Game, Render, Worker };
int mode();
void set_mode(int mode);
const char* status();
void add(Role role);
void remove();
}
