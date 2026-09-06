#pragma once

// Matematica 3D desde el inicio (SPEC.md #6.4), aunque en modo 2D 'z' sea solo clave de
// ordenacion. Envoltorio fino sobre HandmadeMath: el resto del motor usa estos alias, no
// los nombres HMM_* directamente, para poder cambiar de libreria sin tocar el resto del
// codigo.

#include <HandmadeMath.h>

using Vec2 = HMM_Vec2;
using Vec3 = HMM_Vec3;
using Vec4 = HMM_Vec4;
using Mat4 = HMM_Mat4;
using Quat = HMM_Quat;
