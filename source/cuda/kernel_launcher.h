//
// Hypermorph - Fluid Simulator for interactive applications
// Copyright (C) 2016. JIANWEN TAN(jianwen.tan@gmail.com). All rights reserved.
//
// Hypermorph license (* see part 1 below)
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

#ifndef _KERNEL_LAUNCHER_H_
#define _KERNEL_LAUNCHER_H_

#include "third_party/opengl/glew.h"

#include <cuda_runtime.h>
#include <stdint.h>

#include "third_party/glm/fwd.hpp"

struct FlipParticles;
class AuxBufferManager;
class BlockArrangement;
class MemPiece;
enum AdvectionMethod;
enum FluidImpulse;

namespace kern_launcher
{
extern void ClearVolume(cudaArray* dest_array, const float4& value, const uint3& volume_size, BlockArrangement* ba);
extern void CopyToVbo(void* point_vbo, void* extra_vbo, uint16_t* pos_x, uint16_t* pos_y, uint16_t* pos_z, uint16_t* density, uint16_t* temperature, float crit_density, int* num_of_active_particles, int num_of_particles, BlockArrangement* ba);
extern void Raycast(cudaArray* dest_array, cudaArray* density_array, const glm::mat4& inv_rotation, const glm::ivec2& surface_size, const glm::vec3& eye_pos, const glm::vec3& light_color, const glm::vec3& light_pos, float light_intensity, float focal_length, const glm::vec2& screen_size, int num_samples, int num_light_samples, float absorption, float density_factor, float occlusion_factor, const glm::vec3& volume_size);

extern void ApplyBuoyancy(cudaArray* vnp1_x, cudaArray* vnp1_y, cudaArray* vnp1_z, cudaArray* vn_x, cudaArray* vn_y, cudaArray* vn_z, cudaArray* temperature, cudaArray* density, float time_step, float ambient_temperature, float accel_factor, float gravity, bool staggered, uint3 volume_size, BlockArrangement* ba);
extern void ComputeDivergence(cudaArray* div, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, float cell_size, bool outflow, bool staggered, uint3 volume_size, BlockArrangement* ba);
extern void ComputeResidualDiagnosis(cudaArray* residual, cudaArray* u, cudaArray* b, float cell_size, uint3 volume_size, BlockArrangement* ba);
extern void DecayVelocity(cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, float time_step, float velocity_dissipation, const uint3& volume_size, BlockArrangement* ba);
extern void ImpulseVelocity(cudaArray* vnp1_x, cudaArray* vnp1_y, cudaArray* vnp1_z, float3 center, float radius, const float3& value, FluidImpulse impulse, uint3 volume_size, BlockArrangement* ba);
extern void Relax(cudaArray* unp1, cudaArray* un, cudaArray* b, bool outflow, int num_of_iterations, uint3 volume_size, BlockArrangement* ba);
extern void RoundPassed(int* dest_array, int round, int x);
extern void SubtractGradient(cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* pressure, float cell_size, bool staggered, uint3 volume_size, BlockArrangement* ba);

extern void AdvectScalarField(cudaArray* fnp1, cudaArray* fn, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float cell_size, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, bool mid_point, BlockArrangement* ba);
extern void AdvectScalarFieldStaggered(cudaArray* fnp1, cudaArray* fn, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float cell_size, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, bool mid_point, BlockArrangement* ba);
extern void AdvectVectorField(cudaArray* fnp1_x, cudaArray* fnp1_y, cudaArray* fnp1_z, cudaArray* fn_x, cudaArray* fn_y, cudaArray* fn_z, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float cell_size, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, bool mid_point, BlockArrangement* ba);
extern void AdvectVelocityStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y, cudaArray* fnp1_z, cudaArray* fn_x, cudaArray* fn_y, cudaArray* fn_z, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float cell_size, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, bool mid_point, BlockArrangement* ba);
extern void AdvectVorticityStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y, cudaArray* fnp1_z, cudaArray* fn_x, cudaArray* fn_y, cudaArray* fn_z, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float cell_size, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, bool mid_point, BlockArrangement* ba);
extern void ImpulseDensity(cudaArray* dest, cudaArray* original, float3 center_point, float radius, float value, FluidImpulse impulse, uint3 volume_size, BlockArrangement* ba);
extern void ImpulseScalar(cudaArray* dest, cudaArray* original, float3 center_point, float3 hotspot, float radius, float value, FluidImpulse impulse, uint3 volume_size, BlockArrangement* ba);

// Multigrid.
extern void ComputeResidual(cudaArray* r, cudaArray* u, cudaArray* b, uint3 volume_size, BlockArrangement* ba);
extern void Prolongate(cudaArray* fine, cudaArray* coarse, uint3 volume_size_fine, BlockArrangement* ba);
extern void ProlongateError(cudaArray* fine, cudaArray* coarse, uint3 volume_size_fine, BlockArrangement* ba);
extern void RelaxWithZeroGuess(cudaArray* u, cudaArray* b, uint3 volume_size, BlockArrangement* ba);
extern void Restrict(cudaArray* coarse, cudaArray* fine, uint3 volume_size, BlockArrangement* ba);

// Conjugate gradient.
extern void ApplyStencil(cudaArray* aux, cudaArray* search, bool outflow, uint3 volume_size, BlockArrangement* ba);
extern void ComputeAlpha(const MemPiece& alpha, const MemPiece& rho, cudaArray* vec0, cudaArray* vec1, uint3 volume_size, BlockArrangement* ba, AuxBufferManager* bm);
extern void ComputeRho(const MemPiece& rho, cudaArray* search, cudaArray* residual, uint3 volume_size, BlockArrangement* ba, AuxBufferManager* bm);
extern void ComputeRhoAndBeta(const MemPiece& beta, const MemPiece& rho_new, const MemPiece& rho, cudaArray* vec0, cudaArray* vec1, uint3 volume_size, BlockArrangement* ba, AuxBufferManager* bm);
extern void ScaledAdd(cudaArray* dest, cudaArray* v0, cudaArray* v1, const MemPiece& coef, float sign, uint3 volume_size, BlockArrangement* ba);

// Vorticity.
extern void AddCurlPsi(cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* psi_x, cudaArray* psi_y, cudaArray* psi_z, float cell_size, uint3 volume_size, BlockArrangement* ba);
extern void ApplyVorticityConfinementStaggered(cudaArray* vel_x, cudaArray* vely, cudaArray* vel_z, cudaArray* conf_x, cudaArray* conf_y, cudaArray* conf_z, uint3 volume_size, BlockArrangement* ba);
extern void BuildVorticityConfinementStaggered(cudaArray* conf_x, cudaArray* conf_y, cudaArray* conf_z, cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z, float coeff, float cell_size, uint3 volume_size, BlockArrangement* ba);
extern void ComputeCurlStaggered(cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, float cell_size, uint3 volume_size, BlockArrangement* ba);
extern void ComputeDeltaVorticity(cudaArray* delta_x, cudaArray* delta_y, cudaArray* delta_z, cudaArray* vn_x, cudaArray* vn_y, cudaArray* vn_z, uint3 volume_size, BlockArrangement* ba);
extern void DecayVorticesStaggered(cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z, cudaArray* div, float time_step, uint3 volume_size, BlockArrangement* ba);
extern void StretchVorticesStaggered(cudaArray* vnp1_x, cudaArray* vnp1_y, cudaArray* vnp1_z, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z, float cell_size, float time_step, uint3 volume_size, BlockArrangement* ba);

// Particles.
extern void AdvectFlipParticles(const FlipParticles& particles, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, float time_step, float cell_size, bool outflow, uint3 volume_size, BlockArrangement* ba);
extern void AdvectParticles(uint16_t* pos_x, uint16_t* pos_y, uint16_t* pos_z, uint16_t* density, uint16_t* life, int num_of_particles, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, float time_step, float cell_size, bool outflow, uint3 volume_size, BlockArrangement* ba);
extern void BindParticlesToCells(const FlipParticles& particles, uint3 volume_size, BlockArrangement* ba);
extern void BuildCellOffsets(uint* cell_offsets, const uint* cell_particles_counts, int num_of_cells, BlockArrangement* ba, AuxBufferManager* bm);
extern void DiffuseAndDecay(const FlipParticles& particles, float time_step, float velocity_dissipation, float density_dissipation, float temperature_dissipation, BlockArrangement* ba);
extern void EmitFlipParticles(const FlipParticles& particles, float3 center, float3 hotspot, float radius, float density, float temperature, float3 velocity, FluidImpulse impulse, uint random_seed, uint3 volume_size, BlockArrangement* ba);
extern void EmitParticles(uint16_t* pos_x, uint16_t* pos_y, uint16_t* pos_z, uint16_t* density, uint16_t* life, int* tail, int num_of_particles, int num_to_emit, float3 location, float radius, float density_value, uint random_seed, BlockArrangement* ba);
extern void InterpolateDeltaVelocity(const FlipParticles& particles, cudaArray* vnp1_x, cudaArray* vnp1_y, cudaArray* vnp1_z, cudaArray* vn_x, cudaArray* vn_y, cudaArray* vn_z, BlockArrangement* ba);
extern void Resample(const FlipParticles& particles, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* density, cudaArray* temperature, uint random_seed, uint3 volume_size, BlockArrangement* ba);
extern void ResetParticles(const FlipParticles& particles, uint3 volume_size, BlockArrangement* ba);
extern void SortParticles(FlipParticles particles, int* num_active_particles, FlipParticles aux, uint3 volume_size, BlockArrangement* ba);
extern void TransferToGrid(cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* density, cudaArray* temperature, const FlipParticles& particles, const FlipParticles& aux, uint3 volume_size, BlockArrangement* ba);
}

#endif // _KERNEL_LAUNCHER_H_