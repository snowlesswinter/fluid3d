//
// Fluid3d - Fluid Simulator for interactive applications
// Copyright (C) 2016. JIANWEN TAN(jianwen.tan@gmail.com). All rights reserved.
//
// Fluid3d license (* see part 1 below)
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. Acknowledgement of the
//    original author is required if you publish this in a paper, or use it
//    in a product.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include "flip_impl_cuda.h"

#include <cassert>

#include "third_party/opengl/glew.h"

#include <cuda_runtime.h>
#include <helper_math.h>

#include "cuda/kernel_launcher.h"
#include "cuda/random_helper.h"
#include "flip.h"
#include "third_party/glm/vec3.hpp"

namespace
{
uint3 FromGlmVector(const glm::ivec3& v)
{
    return make_uint3(static_cast<uint>(v.x), static_cast<uint>(v.y),
                      static_cast<uint>(v.z));
}
} // Anonymous namespace.

FlipImplCuda::FlipImplCuda(BlockArrangement* ba, RandomHelper* rand)
    : ba_(ba)
    , rand_(rand)
{

}

FlipImplCuda::~FlipImplCuda()
{

}

void FlipImplCuda::Advect(cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z,
                          cudaArray* density, cudaArray* temperature,
                          const glm::ivec3& volume_size)
{
    FlipParticles p;
    LaunchResample(p, vel_x, vel_y, vel_z, density, temperature,
                   rand_->Iterate(), FromGlmVector(volume_size), ba_);
}
