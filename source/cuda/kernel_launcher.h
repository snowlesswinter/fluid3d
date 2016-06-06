#ifndef _KERNEL_LAUNCHER_H_
#define _KERNEL_LAUNCHER_H_

#include "third_party/opengl/glew.h"

#include <cuda_runtime.h>
#include <stdint.h>

class BlockArrangement;
enum AdvectionMethod;

extern void LaunchAdvectFieldsStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y, cudaArray* fnp1_z, cudaArray* fn_x, cudaArray* fn_y, cudaArray* fn_z, cudaArray* aux, cudaArray* velocity, float time_step, float dissipation, uint3 volume_size, BlockArrangement* ba, AdvectionMethod method);
extern void LaunchAdvectScalar(cudaArray_t dest_array, cudaArray_t velocity_array, cudaArray_t source_array, cudaArray_t intermediate_array, float time_step, float dissipation, bool quadratic_dissipation, uint3 volume_size, AdvectionMethod method);
extern void LaunchAdvectScalarStaggered(cudaArray_t dest_array, cudaArray_t velocity_array, cudaArray_t source_array, cudaArray_t intermediate_array, float time_step, float dissipation, bool quadratic_dissipation, uint3 volume_size, AdvectionMethod method);
extern void LaunchAdvectScalarFieldStaggered(cudaArray* fnp1, cudaArray* fn, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, BlockArrangement* ba);
extern void LaunchAdvectVelocity(cudaArray_t dest_array, cudaArray_t velocity_array, cudaArray_t intermediate_array, float time_step, float time_step_prev, float dissipation, uint3 volume_size, AdvectionMethod method);
extern void LaunchAdvectVelocityStaggered(cudaArray_t dest_array, cudaArray_t velocity_array, cudaArray_t intermediate_array, float time_step, float time_step_prev, float dissipation, uint3 volume_size, AdvectionMethod method);
extern void LaunchAdvectVelocityStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y, cudaArray* fnp1_z, cudaArray* fn_x, cudaArray* fn_y, cudaArray* fn_z, cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* aux, float time_step, float dissipation, AdvectionMethod method, uint3 volume_size, BlockArrangement* ba);
extern void LaunchApplyBuoyancy(cudaArray* dest_array, cudaArray* velocity_array, cudaArray* temperature_array, cudaArray* density_array, float time_step, float ambient_temperature, float accel_factor, float gravity, uint3 volume_size);
extern void LaunchApplyBuoyancyStaggered(cudaArray* dest_array, cudaArray* velocity_array, cudaArray* temperature_array, cudaArray* density_array, float time_step, float ambient_temperature, float accel_factor, float gravity, uint3 volume_size);
extern void LaunchApplyBuoyancyStaggered(cudaArray* vel_x, cudaArray* vel_y, cudaArray* vel_z, cudaArray* temperature, cudaArray* density, float time_step, float ambient_temperature, float accel_factor, float gravity, uint3 volume_size);
extern void LaunchApplyImpulse(cudaArray* dest_array, cudaArray* original_array, float3 center_point, float3 hotspot, float radius, float3 value, uint32_t mask, uint3 volume_size);
extern void LaunchApplyVorticityConfinementStaggered(cudaArray* dest, cudaArray* velocity, cudaArray* conf_x, cudaArray* conf_y, cudaArray* conf_z, uint3 volume_size, BlockArrangement* ba);
extern void LaunchBuildVorticityConfinementStaggered(cudaArray* dest_x, cudaArray* dest_y, cudaArray* dest_z, cudaArray* curl_x, cudaArray* curl_y, cudaArray* curl_z, float coeff, float cell_size, uint3 volume_size, BlockArrangement* ba);
extern void LaunchComputeCurlStaggered(cudaArray* dest_x, cudaArray* dest_y, cudaArray* dest_z, cudaArray* velocity, cudaArray* curl_x, cudaArray* curl_y, cudaArray* curl_z, float inverse_cell_size, uint3 volume_size, BlockArrangement* ba);
extern void LaunchComputeDivergence(cudaArray* dest_array, cudaArray* velocity_array, float half_inverse_cell_size, uint3 volume_size);
extern void LaunchComputeDivergenceStaggered(cudaArray* dest_array, cudaArray* velocity_array, float inverse_cell_size, uint3 volume_size);
extern void LaunchComputeResidualDiagnosis(cudaArray* residual, cudaArray* u, cudaArray* b, float inverse_h_square, uint3 volume_size);
extern void LaunchGenerateHeatSphere(cudaArray* dest, cudaArray* original, float3 center_point, float radius, float3 value, uint3 volume_size, BlockArrangement* ba);
extern void LaunchImpulseDensity(cudaArray* dest_array, cudaArray* original_array, float3 center_point, float radius, float3 value, uint3 volume_size);
extern void LaunchImpulseDensitySphere(cudaArray* dest, cudaArray* original, float3 center_point, float radius, float3 value, uint3 volume_size, BlockArrangement* ba);
extern void LaunchRelax(cudaArray* unp1, cudaArray* un, cudaArray* b, float cell_size, int num_of_iterations, uint3 volume_size, BlockArrangement* ba);
extern void LaunchRoundPassed(int* dest_array, int round, int x);
extern void LaunchSubtractGradient(cudaArray* velocity, cudaArray* pressure, float half_inverse_cell_size, uint3 volume_size, BlockArrangement* ba);
extern void LaunchSubtractGradientStaggered(cudaArray* velocity, cudaArray* pressure, float inverse_cell_size, uint3 volume_size, BlockArrangement* ba);

// Vorticity.
extern void LaunchAddCurlPsi(cudaArray* velocity, cudaArray* psi_x, cudaArray* psi_y, cudaArray* psi_z, float cell_size, uint3 volume_size, BlockArrangement* ba);
extern void LaunchAdvectVorticityStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y, cudaArray* fnp1_z, cudaArray* fn_x, cudaArray* fn_y, cudaArray* fn_z, cudaArray* aux, cudaArray* velocity, float time_step, float dissipation, uint3 volume_size, BlockArrangement* ba, AdvectionMethod method);
extern void LaunchComputeDivergenceStaggeredForVort(cudaArray* div, cudaArray* velocity, float cell_size, uint3 volume_size);
extern void LaunchComputeDeltaVorticity(cudaArray* vnp1_x, cudaArray* vnp1_y, cudaArray* vnp1_z, cudaArray* vn_x, cudaArray* vn_y, cudaArray* vn_z, uint3 volume_size, BlockArrangement* ba);
extern void LaunchDecayVorticesStaggered(cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z, cudaArray* div, float time_step, uint3 volume_size, BlockArrangement* ba);
extern void LaunchStretchVorticesStaggered(cudaArray* vort_np1_x, cudaArray* vort_np1_y, cudaArray* vort_np1_z, cudaArray* velocity, cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z, float cell_size, float time_step, uint3 volume_size, BlockArrangement* ba);

#endif // _KERNEL_LAUNCHER_H_