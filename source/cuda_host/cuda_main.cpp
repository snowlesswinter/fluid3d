#include "stdafx.h"
#include "cuda_main.h"

#include <cassert>
#include <algorithm>

#include "cuda/cuda_core.h"
#include "cuda/fluid_impl_cuda.h"
#include "cuda/graphics_resource.h"
#include "cuda/multigrid_impl_cuda.h"
#include "cuda_mem_piece.h"
#include "cuda_volume.h"
#include "opengl/gl_surface.h"
#include "opengl/gl_volume.h"
#include "utility.h"
#include "third_party/glm/vec2.hpp"
#include "third_party/glm/vec3.hpp"

namespace
{
::AdvectionMethod ToCudaAdvectionMethod(CudaMain::AdvectionMethod method)
{
    switch (method) {
        case CudaMain::SEMI_LAGRANGIAN:
            return ::SEMI_LAGRANGIAN;
        case CudaMain::MACCORMACK_SEMI_LAGRANGIAN:
            return ::MACCORMACK_SEMI_LAGRANGIAN;
        case CudaMain::BFECC_SEMI_LAGRANGIAN:
            return ::BFECC_SEMI_LAGRANGIAN;
        default:
            break;
    }

    return ::INVALID_ADVECTION_METHOD;
}
} // Anonymous namespace.

CudaMain* CudaMain::Instance()
{
    static CudaMain* instance = nullptr;
    if (!instance) {
        instance = new CudaMain();
        instance->Init();
    }

    return instance;
}

void CudaMain::DestroyInstance()
{
    Instance()->core_->FlushProfilingData();
    delete Instance();
}

CudaMain::CudaMain()
    : core_(new CudaCore())
    , fluid_impl_(new FluidImplCuda(core_->block_arrangement()))
    , multigrid_impl_(
        new MultigridImplCuda(core_->block_arrangement(),
                              core_->buffer_manager()))
    , registerd_textures_()
{

}

CudaMain::~CudaMain()
{
}

bool CudaMain::Init()
{
    return core_->Init();
}

void CudaMain::ClearVolume(CudaVolume* dest, const glm::vec4& value,
                           const glm::ivec3& volume_size)
{
    core_->ClearVolume(dest->dev_array(), value, volume_size);
}

int CudaMain::RegisterGLImage(std::shared_ptr<GLTexture> texture)
{
    if (registerd_textures_.find(texture) != registerd_textures_.end())
        return 0;

    std::unique_ptr<GraphicsResource> g(new GraphicsResource(core_.get()));
    int r = core_->RegisterGLImage(texture->texture_handle(), texture->target(),
                                   g.get());
    if (r)
        return r;

    registerd_textures_.insert(std::make_pair(texture, std::move(g)));
    return 0;
}

void CudaMain::UnregisterGLImage(std::shared_ptr<GLTexture> texture)
{
    auto i = registerd_textures_.find(texture);
    assert(i != registerd_textures_.end());
    if (i == registerd_textures_.end())
        return;

    core_->UnregisterGLResource(i->second.get());
    registerd_textures_.erase(i);
}

void CudaMain::AdvectField(std::shared_ptr<CudaVolume> fnp1,
                           std::shared_ptr<CudaVolume> fn,
                           std::shared_ptr<CudaVolume> vel_x,
                           std::shared_ptr<CudaVolume> vel_y,
                           std::shared_ptr<CudaVolume> vel_z,
                           std::shared_ptr<CudaVolume> aux, float cell_size,
                           float time_step, float dissipation)
{
    fluid_impl_->AdvectScalarField(fnp1->dev_array(), fn->dev_array(),
                                   vel_x->dev_array(), vel_y->dev_array(),
                                   vel_z->dev_array(), aux->dev_array(),
                                   cell_size, time_step, dissipation,
                                   fnp1->size());
}

void CudaMain::AdvectVelocity(std::shared_ptr<CudaVolume> vnp1_x,
                              std::shared_ptr<CudaVolume> vnp1_y,
                              std::shared_ptr<CudaVolume> vnp1_z,
                              std::shared_ptr<CudaVolume> vn_x,
                              std::shared_ptr<CudaVolume> vn_y,
                              std::shared_ptr<CudaVolume> vn_z,
                              std::shared_ptr<CudaVolume> aux, float cell_size,
                              float time_step, float dissipation)
{
    fluid_impl_->AdvectVectorFields(vnp1_x->dev_array(), vnp1_y->dev_array(),
                                    vnp1_z->dev_array(), vn_x->dev_array(),
                                    vn_y->dev_array(), vn_z->dev_array(),
                                    vn_x->dev_array(), vn_y->dev_array(),
                                    vn_z->dev_array(), aux->dev_array(),
                                    cell_size, time_step, dissipation,
                                    vnp1_x->size(),
                                    FluidImplCuda::VECTOR_FIELD_VELOCITY);
}

void CudaMain::AdvectVorticity(std::shared_ptr<CudaVolume> vnp1_x,
                               std::shared_ptr<CudaVolume> vnp1_y,
                               std::shared_ptr<CudaVolume> vnp1_z,
                               std::shared_ptr<CudaVolume> vn_x,
                               std::shared_ptr<CudaVolume> vn_y,
                               std::shared_ptr<CudaVolume> vn_z,
                               std::shared_ptr<CudaVolume> vel_x,
                               std::shared_ptr<CudaVolume> vel_y,
                               std::shared_ptr<CudaVolume> vel_z,
                               std::shared_ptr<CudaVolume> aux,
                               float cell_size, float time_step,
                               float dissipation)
{
    fluid_impl_->AdvectVectorFields(vnp1_x->dev_array(), vnp1_y->dev_array(),
                                    vnp1_z->dev_array(), vn_x->dev_array(),
                                    vn_y->dev_array(), vn_z->dev_array(),
                                    vel_x->dev_array(), vel_y->dev_array(),
                                    vel_z->dev_array(), aux->dev_array(), 
                                    cell_size, time_step, dissipation,
                                    vnp1_x->size(),
                                    FluidImplCuda::VECTOR_FIELD_VORTICITY);
}

void CudaMain::ApplyBuoyancy(std::shared_ptr<CudaVolume> vel_x,
                             std::shared_ptr<CudaVolume> vel_y,
                             std::shared_ptr<CudaVolume> vel_z,
                             std::shared_ptr<CudaVolume> temperature,
                             std::shared_ptr<CudaVolume> density,
                             float time_step, float ambient_temperature,
                             float accel_factor, float gravity)
{
    fluid_impl_->ApplyBuoyancy(vel_x->dev_array(), vel_y->dev_array(),
                               vel_z->dev_array(), temperature->dev_array(),
                               density->dev_array(), time_step,
                               ambient_temperature, accel_factor, gravity,
                               vel_x->size());
}

void CudaMain::ApplyImpulseDensity(std::shared_ptr<CudaVolume> density,
                                   const glm::vec3& center_point,
                                   const glm::vec3& hotspot, float radius,
                                   float value)
{
    fluid_impl_->ApplyImpulseDensity(density->dev_array(), center_point,
                                     hotspot, radius, value, density->size());
}

void CudaMain::ApplyImpulse(std::shared_ptr<CudaVolume> dest,
                            std::shared_ptr<CudaVolume> source,
                            const glm::vec3& center_point,
                            const glm::vec3& hotspot, float radius,
                            const glm::vec3& value, uint32_t mask)
{
    fluid_impl_->ApplyImpulse(dest->dev_array(), source->dev_array(),
                              center_point, hotspot, radius, value, mask,
                              dest->size());
}

void CudaMain::ComputeDivergence(std::shared_ptr<CudaVolume> div,
                                 std::shared_ptr<CudaVolume> vel_x,
                                 std::shared_ptr<CudaVolume> vel_y,
                                 std::shared_ptr<CudaVolume> vel_z,
                                 float cell_size)
{
    fluid_impl_->ComputeDivergence(div->dev_array(), vel_x->dev_array(),
                                   vel_y->dev_array(), vel_z->dev_array(),
                                   cell_size, div->size());
}

void CudaMain::ComputeResidualDiagnosis(std::shared_ptr<CudaVolume> residual,
                                        std::shared_ptr<CudaVolume> u,
                                        std::shared_ptr<CudaVolume> b,
                                        float cell_size)
{
    fluid_impl_->ComputeResidualDiagnosis(residual->dev_array(), u->dev_array(),
                                          b->dev_array(), cell_size,
                                          residual->size());

    // =========================================================================
    int w = residual->width();
    int h = residual->height();
    int d = residual->depth();
    int n = 1;
    int element_size = sizeof(float);

    static char* buf = nullptr;
    if (!buf)
        buf = new char[w * h * d * element_size * n];

    memset(buf, 0, w * h * d * element_size * n);
    CudaCore::CopyFromVolume(buf, w * element_size * n, residual->dev_array(),
                             residual->size());

    float* f = (float*)buf;
    double sum = 0.0;
    double q = 0.0;
    double m = 0.0;
    for (int i = 0; i < d; i++) {
        for (int j = 0; j < h; j++) {
            for (int k = 0; k < w; k++) {
                for (int l = 0; l < n; l++) {
                    q = f[i * w * h * n + j * w * n + k * n + l];
                    //if (i == 30 && j == 0 && k == 56)
                    //if (q > 1)
                    sum += q;
                    m = std::max(q, m);
                }
            }
        }
    }

    double avg = sum / (w * h * d);
    PrintDebugString("(CUDA) avg ||r||: %.8f,    max ||r||: %.8f\n", avg, m);
}

void CudaMain::Relax(std::shared_ptr<CudaVolume> unp1,
                     std::shared_ptr<CudaVolume> un,
                     std::shared_ptr<CudaVolume> b, float cell_size,
                     int num_of_iterations)
{
    fluid_impl_->Relax(unp1->dev_array(), un->dev_array(), b->dev_array(),
                       cell_size, num_of_iterations, unp1->size());
}

void CudaMain::ReviseDensity(std::shared_ptr<CudaVolume> density,
                             const glm::vec3& center_point, float radius,
                             float value)
{
    fluid_impl_->ReviseDensity(density->dev_array(), center_point, radius,
                               value, density->size());
}

void CudaMain::SubtractGradient(std::shared_ptr<CudaVolume> vel_x,
                                std::shared_ptr<CudaVolume> vel_y,
                                std::shared_ptr<CudaVolume> vel_z,
                                std::shared_ptr<CudaVolume> pressure,
                                float cell_size)
{
    fluid_impl_->SubtractGradient(vel_x->dev_array(), vel_y->dev_array(),
                                  vel_z->dev_array(), pressure->dev_array(),
                                  cell_size, vel_x->size());
}

void CudaMain::ComputeResidual(std::shared_ptr<CudaVolume> r,
                               std::shared_ptr<CudaVolume> u,
                               std::shared_ptr<CudaVolume> b, float cell_size)
{
    multigrid_impl_->ComputeResidual(r->dev_array(), u->dev_array(),
                                     b->dev_array(), cell_size, r->size());
}

void CudaMain::Prolongate(std::shared_ptr<CudaVolume> fine,
                          std::shared_ptr<CudaVolume> coarse)
{
    multigrid_impl_->Prolongate(fine->dev_array(), coarse->dev_array(),
                                fine->size());
}

void CudaMain::ProlongateError(std::shared_ptr<CudaVolume> fine,
                               std::shared_ptr<CudaVolume> coarse)
{
    multigrid_impl_->ProlongateError(fine->dev_array(), coarse->dev_array(),
                                     fine->size());
}

void CudaMain::RelaxWithZeroGuess(std::shared_ptr<CudaVolume> u,
                                  std::shared_ptr<CudaVolume> b,
                                  float cell_size)
{
    multigrid_impl_->RelaxWithZeroGuess(u->dev_array(), b->dev_array(),
                                        cell_size, u->size());
}

void CudaMain::Restrict(std::shared_ptr<CudaVolume> coarse,
                        std::shared_ptr<CudaVolume> fine)
{
    multigrid_impl_->Restrict(coarse->dev_array(), fine->dev_array(),
                              coarse->size());
}

void CudaMain::ApplyStencil(std::shared_ptr<CudaVolume> aux,
                            std::shared_ptr<CudaVolume> search, float cell_size)
{
    multigrid_impl_->ApplyStencil(aux->dev_array(), search->dev_array(),
                                  cell_size, aux->size());
}

void CudaMain::ComputeAlpha(std::shared_ptr<CudaMemPiece> alpha,
                            std::shared_ptr<CudaMemPiece> rho,
                            std::shared_ptr<CudaVolume> aux,
                            std::shared_ptr<CudaVolume> search)
{
    multigrid_impl_->ComputeAlpha(reinterpret_cast<float*>(alpha->mem()),
                                  reinterpret_cast<float*>(rho->mem()),
                                  aux->dev_array(), search->dev_array(),
                                  aux->size());
}

void CudaMain::ComputeRho(std::shared_ptr<CudaMemPiece> rho,
                          std::shared_ptr<CudaVolume> aux,
                          std::shared_ptr<CudaVolume> r)
{
    multigrid_impl_->ComputeRho(reinterpret_cast<float*>(rho->mem()),
                                aux->dev_array(), r->dev_array(), aux->size());
}

void CudaMain::ComputeRhoAndBeta(std::shared_ptr<CudaMemPiece> beta,
                                 std::shared_ptr<CudaMemPiece> rho_new,
                                 std::shared_ptr<CudaMemPiece> rho,
                                 std::shared_ptr<CudaVolume> aux,
                                 std::shared_ptr<CudaVolume> residual)
{
    multigrid_impl_->ComputeRhoAndBeta(reinterpret_cast<float*>(beta->mem()),
                                       reinterpret_cast<float*>(rho_new->mem()),
                                       reinterpret_cast<float*>(rho->mem()),
                                       aux->dev_array(), residual->dev_array(),
                                       aux->size());
}

void CudaMain::UpdateVector(std::shared_ptr<CudaVolume> dest,
                            std::shared_ptr<CudaVolume> v,
                            std::shared_ptr<CudaMemPiece> alpha, float sign)
{
    multigrid_impl_->UpdateVector(dest->dev_array(), v->dev_array(),
                                  reinterpret_cast<float*>(alpha->mem()), sign,
                                  dest->size());
}

void CudaMain::AddCurlPsi(std::shared_ptr<CudaVolume> vel_x,
                          std::shared_ptr<CudaVolume> vel_y,
                          std::shared_ptr<CudaVolume> vel_z,
                          std::shared_ptr<CudaVolume> psi_x,
                          std::shared_ptr<CudaVolume> psi_y,
                          std::shared_ptr<CudaVolume> psi_z, float cell_size)
{
    fluid_impl_->AddCurlPsi(vel_x->dev_array(), vel_y->dev_array(),
                            vel_z->dev_array(), psi_x->dev_array(),
                            psi_y->dev_array(), psi_z->dev_array(), cell_size,
                            vel_x->size());
}

void CudaMain::ApplyVorticityConfinement(std::shared_ptr<CudaVolume> vel_x,
                                         std::shared_ptr<CudaVolume> vel_y,
                                         std::shared_ptr<CudaVolume> vel_z,
                                         std::shared_ptr<CudaVolume> vort_x,
                                         std::shared_ptr<CudaVolume> vort_y,
                                         std::shared_ptr<CudaVolume> vort_z)
{
    fluid_impl_->ApplyVorticityConfinement(vel_x->dev_array(),
                                           vel_y->dev_array(),
                                           vel_z->dev_array(),
                                           vort_x->dev_array(),
                                           vort_y->dev_array(),
                                           vort_z->dev_array(),
                                           vel_x->size());
}

void CudaMain::BuildVorticityConfinement(std::shared_ptr<CudaVolume> conf_x,
                                         std::shared_ptr<CudaVolume> conf_y,
                                         std::shared_ptr<CudaVolume> conf_z,
                                         std::shared_ptr<CudaVolume> vort_x,
                                         std::shared_ptr<CudaVolume> vort_y,
                                         std::shared_ptr<CudaVolume> vort_z,
                                         float coeff, float cell_size)
{
    fluid_impl_->BuildVorticityConfinement(conf_x->dev_array(),
                                           conf_y->dev_array(),
                                           conf_z->dev_array(),
                                           vort_x->dev_array(),
                                           vort_y->dev_array(),
                                           vort_z->dev_array(), coeff,
                                           cell_size, conf_x->size());
}

void CudaMain::ComputeCurl(std::shared_ptr<CudaVolume> vort_x,
                           std::shared_ptr<CudaVolume> vort_y,
                           std::shared_ptr<CudaVolume> vort_z,
                           std::shared_ptr<CudaVolume> vel_x,
                           std::shared_ptr<CudaVolume> vel_y,
                           std::shared_ptr<CudaVolume> vel_z, float cell_size)
{
    fluid_impl_->ComputeCurl(vort_x->dev_array(), vort_y->dev_array(),
                             vort_z->dev_array(), vel_x->dev_array(),
                             vel_y->dev_array(), vel_z->dev_array(), cell_size,
                             vort_x->size());
}

void CudaMain::ComputeDeltaVorticity(std::shared_ptr<CudaVolume> delta_x,
                                     std::shared_ptr<CudaVolume> delta_y,
                                     std::shared_ptr<CudaVolume> delta_z,
                                     std::shared_ptr<CudaVolume> vort_x,
                                     std::shared_ptr<CudaVolume> vort_y,
                                     std::shared_ptr<CudaVolume> vort_z)
{
    fluid_impl_->ComputeDeltaVorticity(delta_x->dev_array(),
                                       delta_y->dev_array(),
                                       delta_z->dev_array(),
                                       vort_x->dev_array(), vort_y->dev_array(),
                                       vort_z->dev_array(), delta_x->size());
}

void CudaMain::DecayVortices(std::shared_ptr<CudaVolume> vort_x,
                             std::shared_ptr<CudaVolume> vort_y,
                             std::shared_ptr<CudaVolume> vort_z,
                             std::shared_ptr<CudaVolume> div, float time_step)
{
    fluid_impl_->DecayVortices(vort_x->dev_array(), vort_y->dev_array(),
                               vort_z->dev_array(), div->dev_array(), time_step,
                               vort_x->size());
}

void CudaMain::StretchVortices(std::shared_ptr<CudaVolume> vnp1_x,
                               std::shared_ptr<CudaVolume> vnp1_y,
                               std::shared_ptr<CudaVolume> vnp1_z,
                               std::shared_ptr<CudaVolume> vel_x,
                               std::shared_ptr<CudaVolume> vel_y,
                               std::shared_ptr<CudaVolume> vel_z,
                               std::shared_ptr<CudaVolume> vort_x,
                               std::shared_ptr<CudaVolume> vort_y,
                               std::shared_ptr<CudaVolume> vort_z,
                               float cell_size, float time_step)
{
    fluid_impl_->StretchVortices(vnp1_x->dev_array(), vnp1_y->dev_array(),
                                 vnp1_z->dev_array(), vel_x->dev_array(),
                                 vel_y->dev_array(), vel_z->dev_array(),
                                 vort_x->dev_array(), vort_y->dev_array(),
                                 vort_z->dev_array(), cell_size, time_step,
                                 vnp1_x->size());
}

void CudaMain::Raycast(std::shared_ptr<GLSurface> dest,
                       std::shared_ptr<CudaVolume> density,
                       const glm::mat4& model_view, const glm::vec3& eye_pos,
                       const glm::vec3& light_color, float light_intensity,
                       float focal_length, int num_samples,
                       int num_light_samples, float absorption,
                       float density_factor, float occlusion_factor)
{
    auto i = registerd_textures_.find(dest);
    assert(i != registerd_textures_.end());
    if (i == registerd_textures_.end())
        return;

    core_->Raycast(i->second.get(), density->dev_array(), model_view,
                   dest->size(), eye_pos, light_color, light_intensity,
                   focal_length, num_samples, num_light_samples, absorption,
                   density_factor, occlusion_factor);
}

void CudaMain::SetAdvectionMethod(AdvectionMethod method)
{
    fluid_impl_->set_advect_method(ToCudaAdvectionMethod(method));
}

void CudaMain::SetMidPoint(bool mid_point)
{
    fluid_impl_->set_mid_point(mid_point);
}

void CudaMain::SetStaggered(bool staggered)
{
    fluid_impl_->set_staggered(staggered);
}

void CudaMain::RoundPassed(int round)
{
    fluid_impl_->RoundPassed(round);
}

void CudaMain::Sync()
{
    core_->Sync();
}
