#ifndef _FLUID_IMPL_CUDA_H_
#define _FLUID_IMPL_CUDA_H_

#include <memory>

#include "advection_method.h"
#include "third_party/glm/fwd.hpp"

struct cudaArray;
class CudaArrayGroup;
class BlockArrangement;
class CudaVolume;
class GraphicsResource;
class FluidImplCuda
{
public:
    explicit FluidImplCuda(BlockArrangement* ba);
    ~FluidImplCuda();

    void Advect(cudaArray* dest, cudaArray* velocity, cudaArray* source,
                cudaArray* intermediate, float time_step, float dissipation,
                const glm::ivec3& volume_size, AdvectionMethod method);
    void AdvectDensity(cudaArray* dest, cudaArray* velocity, cudaArray* density,
                       cudaArray* intermediate, float time_step,
                       float dissipation, const glm::ivec3& volume_size,
                       AdvectionMethod method);
    void AdvectFields(cudaArray* fnp1_x, cudaArray* fnp1_y,
                      cudaArray* fnp1_z, cudaArray* fn_x,
                      cudaArray* fn_y, cudaArray* fn_z, cudaArray* aux,
                      cudaArray* velocity, float time_step, float dissipation,
                      const glm::ivec3& volume_size);
    void AdvectVorticityFields(cudaArray* fnp1_x, cudaArray* fnp1_y,
                               cudaArray* fnp1_z, cudaArray* fn_x,
                               cudaArray* fn_y, cudaArray* fn_z, cudaArray* aux,
                               cudaArray* velocity, float time_step,
                               float dissipation,
                               const glm::ivec3& volume_size);
    void AdvectVelocity(cudaArray* dest, cudaArray* velocity,
                        cudaArray* velocity_prev, float time_step,
                        float time_step_prev, float dissipation,
                        const glm::ivec3& volume_size, AdvectionMethod method);
    void ApplyBuoyancy(cudaArray* dest, cudaArray* velocity,
                       cudaArray* temperature, cudaArray* density,
                       float time_step, float ambient_temperature,
                       float accel_factor, float gravity,
                       const glm::ivec3& volume_size);
    void ApplyImpulse(cudaArray* dest, cudaArray* source,
                      const glm::vec3& center_point,
                      const glm::vec3& hotspot, float radius,
                      const glm::vec3& value, uint32_t mask,
                      const glm::ivec3& volume_size);
    void ApplyImpulseDensity(cudaArray* density, const glm::vec3& center_point,
                             const glm::vec3& hotspot, float radius,
                             float value, const glm::ivec3& volume_size);
    void ApplyVorticityConfinement(cudaArray* dest, cudaArray* velocity,
                                   cudaArray* conf_x, cudaArray* conf_y,
                                   cudaArray* conf_z,
                                   const glm::ivec3& volume_size);
    void BuildVorticityConfinement(cudaArray* dest_x, cudaArray* dest_y,
                                   cudaArray* dest_z, cudaArray* curl_x,
                                   cudaArray* curl_y, cudaArray* curl_z,
                                   float coeff, float cell_size,
                                   const glm::ivec3& volume_size);
    void ComputeCurl(cudaArray* dest_x, cudaArray* dest_y, cudaArray* dest_z,
                     cudaArray* velocity, cudaArray* curl_x, cudaArray* curl_y,
                     cudaArray* curl_z, float inverse_cell_size,
                     const glm::ivec3& volume_size);
    void ComputeDivergence(cudaArray* dest, cudaArray* velocity,
                           float half_inverse_cell_size,
                           const glm::ivec3& volume_size);
    void ComputeResidualDiagnosis(cudaArray* residual, cudaArray* u,
                                  cudaArray* b, float inverse_h_square,
                                  const glm::ivec3& volume_size);
    void Relax(cudaArray* unp1, cudaArray* un, cudaArray* b, float cell_size,
               int num_of_iterations, const glm::ivec3& volume_size);
    void ReviseDensity(cudaArray* density, const glm::vec3& center_point,
                       float radius, float value,
                       const glm::ivec3& volume_size);
    void SubtractGradient(cudaArray* velocity, cudaArray* pressure,
                          float gradient_scale,
                          const glm::ivec3& volume_size);

    // Vorticity.
    void AddCurlPsi(cudaArray* velocity, cudaArray* psi_x, cudaArray* psi_y,
                    cudaArray* psi_z, float cell_size,
                    const glm::ivec3& volume_size);
    void ComputeDeltaVorticity(cudaArray* vort_np1_x, cudaArray* vort_np1_y,
                               cudaArray* vort_np1_z, cudaArray* vort_x,
                               cudaArray* vort_y, cudaArray* vort_z,
                               const glm::ivec3& volume_size);
    void ComputeDivergenceForVort(cudaArray* div, cudaArray* velocity,
                                  float cell_size,
                                  const glm::ivec3& volume_size);
    void DecayVortices(cudaArray* vort_x, cudaArray* vort_y, cudaArray* vort_z,
                       cudaArray* div, float time_step,
                       const glm::ivec3& volume_size);
    void StretchVortices(cudaArray* vort_np1_x, cudaArray* vort_np1_y,
                         cudaArray* vort_np1_z, cudaArray* velocity,
                         cudaArray* vort_x, cudaArray* vort_y,
                         cudaArray* vort_z, float cell_size, float time_step,
                         const glm::ivec3& volume_size);

    // For debugging.
    void RoundPassed(int round);

    void set_staggered(bool staggered) { staggered_ = staggered; }

private:
    BlockArrangement* ba_;
    bool staggered_;
};

#endif // _FLUID_IMPL_CUDA_H_