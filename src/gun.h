#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* WonderFitter gun state reported by WonderFitterGun.java (any thread). */
bool tremu_gun_connected(void);
/* The gun sends no "released" frame: during full auto a shot arrives about every
 * 100 ms, so the trigger counts as held while the last shot is recent. */
bool tremu_gun_trigger_held(uint64_t now_ns);
/* Charging handle pull, stretched into a short press so the game samples it. */
bool tremu_gun_bolt_pressed(uint64_t now_ns);
/* -1 no gun, 0 magazine removed, 1 inserted. */
int tremu_gun_magazine(void);
/* Shots fired since start (one per shot frame): a change means a new shot. */
unsigned tremu_gun_shots(void);
/* Name of the last gun connected (e.g. "M416"); returns a counter that changes on each connection. */
unsigned tremu_gun_name(char *out, size_t size);
