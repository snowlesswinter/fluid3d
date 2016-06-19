#include "stdafx.h"
#include "fluid_simulator.h"

#include <cassert>

#include "cuda_host/cuda_main.h"
#include "cuda_host/cuda_volume.h"
#include "fluid_config.h"
#include "graphics_volume.h"
#include "metrics.h"
#include "opengl/gl_volume.h"
#include "poisson_solver/full_multigrid_poisson_solver.h"
#include "poisson_solver/multigrid_core_cuda.h"
#include "poisson_solver/multigrid_core_glsl.h"
#include "poisson_solver/multigrid_poisson_solver.h"
#include "poisson_solver/open_boundary_multigrid_poisson_solver.h"
#include "poisson_solver/preconditioned_conjugate_gradient.h"
#include "shader/fluid_shader.h"
#include "shader/multigrid_shader.h"
#include "third_party/glm/vec2.hpp"
#include "third_party/glm/vec3.hpp"
#include "third_party/opengl/glew.h"
#include "utility.h"

static struct
{
    GLuint Advect;
    GLuint Jacobi;
    GLuint DampedJacobi;
    GLuint compute_residual;
    GLuint SubtractGradient;
    GLuint ComputeDivergence;
    GLuint ApplyImpulse;
    GLuint ApplyBuoyancy;
    GLuint diagnose_;
} Programs;

enum DiagnosisTarget
{
    DIAG_NONE,
    DIAG_VELOCITY,
    DIAG_PRESSURE,
    DIAG_CURL,
    DIAG_DELTA_VORT,
    DIAG_PSI,

    NUM_DIAG_TARGETS
};

FluidSimulator::FluidSimulator()
    : grid_size_(128.0f)
    , cell_size_(0.15f)
    , graphics_lib_(GRAPHICS_LIB_CUDA)
    , solver_choice_(POISSON_SOLVER_FULL_MULTI_GRID)
    , multigrid_core_()
    , pressure_solver_()
    , psi_solver_()
    , volume_byte_width_(2)
    , diagnosis_(0)
    , velocity_(GRAPHICS_LIB_CUDA)
    , velocity_prime_(GRAPHICS_LIB_CUDA)
    , vorticity_(GRAPHICS_LIB_CUDA)
    , aux_(GRAPHICS_LIB_CUDA)
    , vort_conf_(GRAPHICS_LIB_CUDA)
    , density_()
    , temperature_()
    , general1a_()
    , general1b_()
    , general1c_()
    , general1d_()
    , diagnosis_volume_()
    , manual_impulse_()
{
}

FluidSimulator::~FluidSimulator()
{
}

bool FluidSimulator::Init()
{
    density_ = std::make_shared<GraphicsVolume>(graphics_lib_);
    temperature_ = std::make_shared<GraphicsVolume>(graphics_lib_);
    general1a_ = std::make_shared<GraphicsVolume>(graphics_lib_);
    general1b_ = std::make_shared<GraphicsVolume>(graphics_lib_);
    general1c_ = std::make_shared<GraphicsVolume>(graphics_lib_);
    general1d_ = std::make_shared<GraphicsVolume>(graphics_lib_);

    // A hard lesson had told us: locality is a vital factor of the performance
    // of raycast. Even a trivial-like adjustment that packing the temperature
    // with the density field would surprisingly bring a 17% decline to the
    // performance. 
    //
    // Here is the analysis:
    // 
    // In the original design, density buffer is 128 ^ 3 * 2 byte = 4 MB,
    // where as the buffer had been increased to 128 ^ 3 * 6 byte = 12 MB in
    // our experiment(it is 6 bytes wide instead of 4 because we need to
    // swap it with the 3-byte-width buffer that shared with velocity buffer).
    // That expanded buffer size would greatly increase the possibility of
    // cache miss in GPU during raycast. So, it's a problem all about the cache
    // shortage in graphic cards.

    grid_size_ = FluidConfig::Instance()->grid_size();
    cell_size_ = FluidConfig::Instance()->cell_size();

    int width = static_cast<int>(grid_size_.x);
    int height = static_cast<int>(grid_size_.y);
    int depth = static_cast<int>(grid_size_.z);

    bool result = density_->Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = velocity_.Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = velocity_prime_.Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = temperature_->Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = general1a_->Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = general1b_->Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = general1c_->Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    result = general1d_->Create(width, height, depth, 1, 2, 0);
    assert(result);
    if (!result)
        return false;

    if (graphics_lib_ == GRAPHICS_LIB_GLSL ||
            graphics_lib_ == GRAPHICS_LIB_CUDA_DIAGNOSIS) {
        Programs.Advect = LoadProgram(FluidShader::Vertex(),
                                      FluidShader::PickLayer(),
                                      FluidShader::Advect());
        Programs.Jacobi = LoadProgram(FluidShader::Vertex(),
                                      FluidShader::PickLayer(),
                                      FluidShader::Jacobi());
        Programs.DampedJacobi = LoadProgram(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            FluidShader::DampedJacobiPacked());
        Programs.compute_residual = LoadProgram(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridShader::ComputeResidual());
        Programs.SubtractGradient = LoadProgram(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            FluidShader::SubtractGradient());
        Programs.ComputeDivergence = LoadProgram(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            FluidShader::ComputeDivergence());
        Programs.ApplyImpulse = LoadProgram(FluidShader::Vertex(),
                                            FluidShader::PickLayer(),
                                            FluidShader::Splat());
        Programs.ApplyBuoyancy = LoadProgram(FluidShader::Vertex(),
                                             FluidShader::PickLayer(),
                                             FluidShader::Buoyancy());
        Programs.diagnose_ = LoadProgram(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridShader::ComputeResidualPackedDiagnosis());
    }

    Reset();
    return true;
}

void FluidSimulator::Reset()
{
    density_->Clear();
    temperature_->Clear();
    general1a_->Clear();
    general1b_->Clear();
    general1c_->Clear();
    general1d_->Clear();

    if (velocity_) {
        velocity_.x()->Clear();
        velocity_.y()->Clear();
        velocity_.z()->Clear();
    }

    if (velocity_prime_) {
        velocity_prime_.x()->Clear();
        velocity_prime_.y()->Clear();
        velocity_prime_.z()->Clear();
    }

    if (vorticity_) {
        vorticity_.x()->Clear();
        vorticity_.y()->Clear();
        vorticity_.z()->Clear();
    }

    if (aux_) {
        aux_.x()->Clear();
        aux_.y()->Clear();
        aux_.z()->Clear();
    }

    diagnosis_volume_.reset();

    Metrics::Instance()->Reset();
}

std::shared_ptr<GraphicsVolume> FluidSimulator::GetDensityField() const
{
    return density_;
}

bool FluidSimulator::IsImpulsing() const
{
    return !!manual_impulse_;
}

void FluidSimulator::NotifyConfigChanged()
{
    CudaMain::Instance()->SetStaggered(FluidConfig::Instance()->staggered());
    CudaMain::Instance()->SetMidPoint(FluidConfig::Instance()->mid_point());
    CudaMain::Instance()->SetOutflow(FluidConfig::Instance()->outflow());
    CudaMain::Instance()->SetAdvectionMethod(
        FluidConfig::Instance()->advection_method());
    CudaMain::Instance()->SetFluidImpulse(
        FluidConfig::Instance()->fluid_impluse());
}

void FluidSimulator::StartImpulsing(float x, float y)
{
    manual_impulse_.reset(new glm::vec2(x, y));
}

void FluidSimulator::StopImpulsing()
{
    manual_impulse_.reset();
}

void FluidSimulator::Update(float delta_time, double seconds_elapsed,
                            int frame_count)
{
    Metrics::Instance()->OnFrameUpdateBegins();

    float fixed_time_step = FluidConfig::Instance()->fixed_time_step();
    float proper_delta_time = fixed_time_step > 0.0f ?
        fixed_time_step : std::min(delta_time, kMaxTimeStep);

    // Splat new smoke
    ApplyImpulse(seconds_elapsed, proper_delta_time);
    Metrics::Instance()->OnImpulseApplied();

    // Calculate divergence.
    ComputeDivergence(general1a_, cell_size_);
    Metrics::Instance()->OnDivergenceComputed();

    // Solve pressure-velocity Poisson equation
    SolvePressure(general1b_, general1a_, cell_size_);
    Metrics::Instance()->OnPressureSolved();

    // Rectify velocity via the gradient of pressure
    SubtractGradient(general1b_, cell_size_);
    Metrics::Instance()->OnVelocityRectified();

    // Advect density and temperature
    AdvectTemperature(cell_size_, proper_delta_time);
    Metrics::Instance()->OnTemperatureAvected();

    AdvectDensity(cell_size_, proper_delta_time);
    Metrics::Instance()->OnDensityAvected();

    // Advect velocity
    AdvectVelocity(cell_size_, proper_delta_time);
    Metrics::Instance()->OnVelocityAvected();

    // Restore vorticity
    RestoreVorticity(proper_delta_time, cell_size_);
    Metrics::Instance()->OnVorticityRestored();

    // Apply buoyancy and gravity
    ApplyBuoyancy(proper_delta_time);
    Metrics::Instance()->OnBuoyancyApplied();

    ReviseDensity();

    // Recently in my experiments I examined the data generated by the passes
    // of simulation(for CUDA porting), and I found that in different times of
    // execution, the results always fluctuate a bit, even through I turned off
    // the random hotspot, this fluctuation remains.
    //
    // This system should have no any undetermined factor and random number
    // introduced, and the exactly same result should be produced every time
    // the simulation ran. The most suspicious part is that the in-place
    // modification pattern accessing the texture in the pressure solver, 
    // which may produce different results due to the undetermined order of
    // shader/kernel execution.
    // I may find some time to explore into it.

    CudaMain::Instance()->RoundPassed(frame_count);
}

void FluidSimulator::UpdateImpulsing(float x, float y)
{
    if (manual_impulse_) {
        *manual_impulse_ = glm::vec2(x, y);
    }
}

void FluidSimulator::set_diagnosis(int diagnosis)
{
    diagnosis_ = diagnosis % NUM_DIAG_TARGETS;
}

void FluidSimulator::AdvectDensity(float cell_size, float delta_time)
{
    float density_dissipation = FluidConfig::Instance()->density_dissipation();
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->AdvectField(general1a_->cuda_volume(),
                                          density_->cuda_volume(),
                                          velocity_.x()->cuda_volume(),
                                          velocity_.y()->cuda_volume(),
                                          velocity_.z()->cuda_volume(),
                                          general1b_->cuda_volume(),cell_size,
                                          delta_time, density_dissipation);
    } else {
        AdvectImpl(density_, delta_time, density_dissipation);
    }
    std::swap(density_, general1a_);
}

void FluidSimulator::AdvectImpl(std::shared_ptr<GraphicsVolume> source,
                                float delta_time, float dissipation)
{
    glUseProgram(Programs.Advect);

    SetUniform("InverseSize", CalculateInverseSize(*source->gl_volume()));
    SetUniform("TimeStep", delta_time);
    SetUniform("Dissipation", dissipation);
    SetUniform("SourceTexture", 1);
    SetUniform("Obstacles", 2);

    glBindFramebuffer(GL_FRAMEBUFFER,
                      general1a_->gl_volume()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, velocity_.x()->gl_volume()->texture_handle());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, source->gl_volume()->texture_handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          general1a_->gl_volume()->depth());
    ResetState();
}

void FluidSimulator::AdvectTemperature(float cell_size, float delta_time)
{
    float temperature_dissipation =
        FluidConfig::Instance()->temperature_dissipation();
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->AdvectField(general1a_->cuda_volume(),
                                          temperature_->cuda_volume(),
                                          velocity_.x()->cuda_volume(),
                                          velocity_.y()->cuda_volume(),
                                          velocity_.z()->cuda_volume(),
                                          general1b_->cuda_volume(), cell_size,
                                          delta_time, temperature_dissipation);
    } else {
        AdvectImpl(temperature_, delta_time, temperature_dissipation);
    }

    std::swap(temperature_, general1a_);
}

void FluidSimulator::AdvectVelocity(float cell_size, float delta_time)
{
    float velocity_dissipation =
        FluidConfig::Instance()->velocity_dissipation();
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->AdvectVelocity(velocity_prime_.x()->cuda_volume(),
                                             velocity_prime_.y()->cuda_volume(),
                                             velocity_prime_.z()->cuda_volume(),
                                             velocity_.x()->cuda_volume(),
                                             velocity_.y()->cuda_volume(),
                                             velocity_.z()->cuda_volume(),
                                             general1a_->cuda_volume(),
                                             cell_size, delta_time,
                                             velocity_dissipation);
        velocity_.Swap(velocity_prime_);

        if (diagnosis_ == DIAG_VELOCITY) {
            CudaMain::Instance()->PrintVolume(velocity_.x()->cuda_volume(),
                                              "VelocityX");
            CudaMain::Instance()->PrintVolume(velocity_.y()->cuda_volume(),
                                              "VelocityY");
            CudaMain::Instance()->PrintVolume(velocity_.z()->cuda_volume(),
                                              "VelocityZ");
        }
    } else {
        glUseProgram(Programs.Advect);

        SetUniform("InverseSize",
                   CalculateInverseSize(*velocity_.x()->gl_volume()));
        SetUniform("TimeStep", delta_time);
        SetUniform("Dissipation", velocity_dissipation);
        SetUniform("SourceTexture", 1);
        SetUniform("Obstacles", 2);

        glBindFramebuffer(GL_FRAMEBUFFER,
                          general1a_->gl_volume()->frame_buffer());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D,
                      velocity_.x()->gl_volume()->texture_handle());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D,
                      velocity_.x()->gl_volume()->texture_handle());
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              general1a_->gl_volume()->depth());
        ResetState();

        //std::swap(velocity2_.x(), general4a_);
    }

}

void FluidSimulator::ApplyBuoyancy(float delta_time)
{
    float smoke_weight = FluidConfig::Instance()->smoke_weight();
    float ambient_temperature = FluidConfig::Instance()->ambient_temperature();
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ApplyBuoyancy(velocity_.x()->cuda_volume(),
                                            velocity_.y()->cuda_volume(),
                                            velocity_.z()->cuda_volume(),
                                            temperature_->cuda_volume(),
                                            density_->cuda_volume(), delta_time,
                                            ambient_temperature,
                                            kBuoyancyCoef, smoke_weight);
    } else {
        glUseProgram(Programs.ApplyBuoyancy);

        SetUniform("Velocity", 0);
        SetUniform("Temperature", 1);
        SetUniform("AmbientTemperature", ambient_temperature);
        SetUniform("TimeStep", delta_time);
        SetUniform("Sigma", kBuoyancyCoef);
        SetUniform("Kappa", smoke_weight);

        glBindFramebuffer(GL_FRAMEBUFFER,
                          general1a_->gl_volume()->frame_buffer());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D,
                      velocity_.x()->gl_volume()->texture_handle());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, temperature_->gl_volume()->texture_handle());
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              general1a_->gl_volume()->depth());
        ResetState();

        //std::swap(velocity_, general4a_);
    }
}

void FluidSimulator::ApplyImpulse(double seconds_elapsed, float delta_time)
{
    glm::vec3 pos = kImpulsePosition * grid_size_;
    double �� = 3.1415926;
    float splat_radius =
        grid_size_.x * FluidConfig::Instance()->splat_radius_factor();
    float time_stretch = FluidConfig::Instance()->time_stretch() + 0.00001f;
    float sin_factor = static_cast<float>(sin(seconds_elapsed / time_stretch * 2.0 * ��));
    float cos_factor = static_cast<float>(cos(seconds_elapsed / time_stretch * 2.0 * ��));
    float hotspot_x = cos_factor * splat_radius * 0.8f + pos.x;
    float hotspot_z = sin_factor * splat_radius * 0.8f + pos.z;
    glm::vec3 hotspot(hotspot_x, 0.0f, hotspot_z);

    if (manual_impulse_)
        hotspot = glm::vec3(0.5f * grid_size_.x * (manual_impulse_->x + 1.0f),
                            0.0f,
                            0.5f * grid_size_.z * (manual_impulse_->y + 1.0f));
    else if (!FluidConfig::Instance()->auto_impulse())
        return;

    CudaMain::FluidImpulse impulse = FluidConfig::Instance()->fluid_impluse();
    if (impulse == CudaMain::IMPULSE_BUOYANT_JET) {
        pos.x = pos.y;
        pos.y = splat_radius + 2;
    }

    ImpulseDensity(pos, hotspot, splat_radius,
                   FluidConfig::Instance()->impulse_density());

    Impulse(temperature_, pos, hotspot, splat_radius,
            FluidConfig::Instance()->impulse_temperature());

    int t = static_cast<int>(seconds_elapsed / time_stretch);
    if (t % 2 && impulse == CudaMain::IMPULSE_BUOYANT_JET) {
        float coef = static_cast<float>(sin(seconds_elapsed * 2.0 * 2.0 * ��));
        float initial_velocity =
            (1.0f + coef * 0.5f) * FluidConfig::Instance()->impulse_velocity();
        Impulse(velocity_.x(), pos, hotspot, splat_radius, initial_velocity);
    }
}

void FluidSimulator::ComputeDivergence(
    std::shared_ptr<GraphicsVolume> divergence, float cell_size)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ComputeDivergence(divergence->cuda_volume(),
                                                velocity_.x()->cuda_volume(),
                                                velocity_.y()->cuda_volume(),
                                                velocity_.z()->cuda_volume(),
                                                cell_size);
    } else {
        float half_inverse_cell_size = 0.5f / cell_size;

        glUseProgram(Programs.ComputeDivergence);

        SetUniform("HalfInverseCellSize", half_inverse_cell_size);
        SetUniform("Obstacles", 1);
        SetUniform("velocity", 0);

        glBindFramebuffer(GL_FRAMEBUFFER,
                          divergence->gl_volume()->frame_buffer());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D,
                      velocity_.x()->gl_volume()->texture_handle());
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              divergence->gl_volume()->depth());
        ResetState();
    }
}

void FluidSimulator::ComputeResidualDiagnosis(float cell_size)
{
    if (diagnosis_ != DIAG_PRESSURE)
        return;

    if (!diagnosis_volume_) {
        int width = static_cast<int>(grid_size_.x);
        int height = static_cast<int>(grid_size_.y);
        int depth = static_cast<int>(grid_size_.z);
        std::shared_ptr<GraphicsVolume> v(new GraphicsVolume(graphics_lib_));
        bool result = v->Create(width, height, depth, 1, 4, 0);
        assert(result);
        if (!result)
            return;

        diagnosis_volume_ = v;
    }

    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ComputeResidualDiagnosis(
            diagnosis_volume_->cuda_volume(), general1b_->cuda_volume(),
            general1a_->cuda_volume(), cell_size);
    } else if (graphics_lib_ == GRAPHICS_LIB_GLSL) {
        float inverse_h_square = 1.0f / (cell_size * cell_size);

        glUseProgram(Programs.diagnose_);

        SetUniform("packed_tex", 0);
        SetUniform("inverse_h_square", inverse_h_square);

        diagnosis_volume_->gl_volume()->BindFrameBuffer();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, general1b_->gl_volume()->texture_handle());
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              diagnosis_volume_->gl_volume()->depth());
        ResetState();

        // =====================================================================
        glFinish();
        GraphicsVolume* p = diagnosis_volume_.get();

        int w = p->GetWidth();
        int h = p->GetHeight();
        int d = p->GetDepth();
        int n = 1;
        int element_size = sizeof(float);
        GLenum format = GL_RED;

        static char* v = nullptr;
        if (!v)
            v = new char[w * h * d * element_size * n];

        memset(v, 0, w * h * d * element_size * n);
        p->gl_volume()->GetTexImage(v);

        float* f = (float*)v;
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
        PrintDebugString("(GLSL) avg ||r||: %.8f,    max ||r||: %.8f\n", avg, m);
    }
}

void FluidSimulator::DampedJacobi(std::shared_ptr<GraphicsVolume> pressure,
                                  std::shared_ptr<GraphicsVolume> divergence,
                                  float cell_size, int num_of_iterations)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->Relax(pressure->cuda_volume(),
                                    pressure->cuda_volume(),
                                    divergence->cuda_volume(), cell_size,
                                    num_of_iterations);
    } else {
        float one_minus_omega = 0.33333333f;
        float minus_square_cell_size = -(cell_size * cell_size);
        float omega_over_beta = 0.11111111f;

        for (int i = 0; i < num_of_iterations; ++i) {
            glUseProgram(Programs.DampedJacobi);

            SetUniform("Alpha", minus_square_cell_size);
            SetUniform("InverseBeta", omega_over_beta);
            SetUniform("one_minus_omega", one_minus_omega);
            SetUniform("packed_tex", 0);

            glBindFramebuffer(GL_FRAMEBUFFER,
                              general1b_->gl_volume()->frame_buffer());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, general1b_->gl_volume()->texture_handle());
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                                  general1b_->gl_volume()->depth());
            ResetState();
        }
    }
}

void FluidSimulator::Impulse(std::shared_ptr<GraphicsVolume> dest,
                             const glm::vec3& position,
                             const glm::vec3& hotspot, float splat_radius,
                             float value)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ApplyImpulse(dest->cuda_volume(),
                                           dest->cuda_volume(),
                                           position, hotspot, splat_radius,
                                           value);
    } else {
        glUseProgram(Programs.ApplyImpulse);

        SetUniform("center_point", position);
        SetUniform("hotspot", hotspot);
        SetUniform("radius", splat_radius);
        SetUniform("fill_color_r", value);
        SetUniform("fill_color_g", value);

        glBindFramebuffer(GL_FRAMEBUFFER, dest->gl_volume()->frame_buffer());
        glEnable(GL_BLEND);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              dest->gl_volume()->depth());
        ResetState();
    }
}

void FluidSimulator::ImpulseDensity(const glm::vec3& position,
                                    const glm::vec3& hotspot,
                                    float splat_radius, float value)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ApplyImpulseDensity(density_->cuda_volume(),
                                                  position, hotspot,
                                                  splat_radius, value);
    } else {
        glUseProgram(Programs.ApplyImpulse);

        SetUniform("center_point", position);
        SetUniform("hotspot", hotspot);
        SetUniform("radius", splat_radius);
        SetUniform("fill_color_r", value);
        SetUniform("fill_color_g", value);

        glBindFramebuffer(GL_FRAMEBUFFER,
                          density_->gl_volume()->frame_buffer());
        glEnable(GL_BLEND);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              density_->gl_volume()->depth());
        ResetState();
    }
}

void FluidSimulator::ReviseDensity()
{
    glm::vec3 pos = kImpulsePosition * grid_size_;
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        if (CudaMain::IMPULSE_HOT_FLOOR ==
                FluidConfig::Instance()->fluid_impluse())
            CudaMain::Instance()->ReviseDensity(
                density_->cuda_volume(), pos, grid_size_.x * 0.5f, 0.1f);
    }
}

void FluidSimulator::SolvePressure(std::shared_ptr<GraphicsVolume> pressure,
                                   std::shared_ptr<GraphicsVolume> divergence,
                                   float cell_size)
{
    if (!multigrid_core_) {
        if (graphics_lib_ == GRAPHICS_LIB_CUDA)
            multigrid_core_.reset(new MultigridCoreCuda());
        else
            multigrid_core_.reset(new MultigridCoreGlsl());
    }

    int num_iterations = 0;
    switch (solver_choice_) {
        case POISSON_SOLVER_JACOBI:
        case POISSON_SOLVER_GAUSS_SEIDEL:
        case POISSON_SOLVER_DAMPED_JACOBI: {
            num_iterations =
                FluidConfig::Instance()->num_jacobi_iterations();
            DampedJacobi(pressure, divergence, cell_size,
                         num_iterations);
            break;
        }
        case POISSON_SOLVER_MULTI_GRID: {
            if (!pressure_solver_) {
                pressure_solver_.reset(
                    new MultigridPoissonSolver(multigrid_core_.get()));
                pressure_solver_->Initialize(pressure->GetWidth(),
                                             pressure->GetHeight(),
                                             pressure->GetDepth(),
                                             volume_byte_width_, 32);
            }
            num_iterations =
                FluidConfig::Instance()->num_multigrid_iterations();
            break;
        }
        case POISSON_SOLVER_FULL_MULTI_GRID: {
            if (!pressure_solver_) {
                pressure_solver_.reset(
                    new FullMultigridPoissonSolver(multigrid_core_.get()));
                pressure_solver_->Initialize(pressure->GetWidth(),
                                             pressure->GetHeight(),
                                             pressure->GetDepth(),
                                             volume_byte_width_, 32);
            }
            num_iterations =
                FluidConfig::Instance()->num_full_multigrid_iterations();
            break;
        }
        case POISSON_SOLVER_MULTI_GRID_PRECONDITIONED_CONJUGATE_GRADIENT: {
            if (!pressure_solver_) {
                pressure_solver_.reset(
                    new PreconditionedConjugateGradient(multigrid_core_.get()));
                pressure_solver_->Initialize(pressure->GetWidth(),
                                             pressure->GetHeight(),
                                             pressure->GetDepth(),
                                             volume_byte_width_, 32);
            }
            num_iterations =
                FluidConfig::Instance()->num_mgpcg_iterations();
            break;
        }
        default: {
            break;
        }
    }

    if (pressure_solver_) {
        pressure_solver_->SetDiagnosis(diagnosis_ == DIAG_PRESSURE);
        pressure_solver_->Solve(pressure, divergence, cell_size,
                                num_iterations);
    }

    ComputeResidualDiagnosis(cell_size);
}

void FluidSimulator::SubtractGradient(std::shared_ptr<GraphicsVolume> pressure,
                                      float cell_size)
{
    // In the original implementation, this coefficient was set to 1.125, which
    // I guess is a trick to compensate the inaccuracy of the solution of
    // Poisson equation. As the solution now becomes more and more precise,
    // I changed the number to 1.0 to keep the system stable.
    //
    // 2016/5/23 update:
    // During the process of verifying the staggered grid discretization, I
    // found this coefficient should be the same as that in divergence
    // calculation. This mistake was introduced at the first day the project
    // was created.
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->SubtractGradient(velocity_.x()->cuda_volume(),
                                               velocity_.y()->cuda_volume(),
                                               velocity_.z()->cuda_volume(),
                                               pressure->cuda_volume(),
                                               cell_size);
    } else {
        const float half_inverse_cell_size = 0.5f / cell_size;

        glUseProgram(Programs.SubtractGradient);

        SetUniform("GradientScale", half_inverse_cell_size);
        SetUniform("velocity", 0);
        SetUniform("packed_tex", 1);

        glBindFramebuffer(GL_FRAMEBUFFER,
                          velocity_.x()->gl_volume()->frame_buffer());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D,
                      velocity_.x()->gl_volume()->texture_handle());
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, pressure->gl_volume()->texture_handle());
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                              velocity_.x()->gl_volume()->depth());
        ResetState();
    }
}

void FluidSimulator::AddCurlPsi(const GraphicsVolume3& psi, float cell_size)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->AddCurlPsi(velocity_.x()->cuda_volume(),
                                         velocity_.y()->cuda_volume(),
                                         velocity_.z()->cuda_volume(),
                                         psi.x()->cuda_volume(),
                                         psi.y()->cuda_volume(),
                                         psi.z()->cuda_volume(), cell_size);
    }
}

void FluidSimulator::AdvectVortices(const GraphicsVolume3& vorticity,
                                    const GraphicsVolume3& temp,
                                    std::shared_ptr<GraphicsVolume> aux,
                                    float cell_size, float delta_time)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->AdvectVorticity(
            vorticity.x()->cuda_volume(), vorticity.y()->cuda_volume(),
            vorticity.z()->cuda_volume(), temp.x()->cuda_volume(),
            temp.y()->cuda_volume(), temp.z()->cuda_volume(),
            velocity_prime_.x()->cuda_volume(),
            velocity_prime_.y()->cuda_volume(),
            velocity_prime_.z()->cuda_volume(), aux->cuda_volume(), cell_size,
            delta_time, 0.0f);
    }
}

void FluidSimulator::ApplyVorticityConfinemnet()
{
    const GraphicsVolume3& vort_conf = GetVorticityConfinementField();
    if (!vort_conf)
        return;

    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ApplyVorticityConfinement(
            velocity_.x()->cuda_volume(), velocity_.y()->cuda_volume(),
            velocity_.z()->cuda_volume(), vort_conf.x()->cuda_volume(),
            vort_conf.y()->cuda_volume(), vort_conf.z()->cuda_volume());
    }

    //std::swap(velocity_, general4a_);
}

void FluidSimulator::BuildVorticityConfinemnet(float delta_time,
                                               float cell_size)
{
    const GraphicsVolume3& vorticity = GetVorticityField();
    if (!vorticity)
        return;

    const GraphicsVolume3& vort_conf = GetVorticityConfinementField();
    if (!vort_conf)
        return;

    float vort_conf_coef = FluidConfig::Instance()->vorticity_confinement();
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->BuildVorticityConfinement(
            vort_conf.x()->cuda_volume(), vort_conf.y()->cuda_volume(),
            vort_conf.z()->cuda_volume(), vorticity.x()->cuda_volume(),
            vorticity.y()->cuda_volume(), vorticity.z()->cuda_volume(),
            vort_conf_coef * delta_time, cell_size);
    }
}

void FluidSimulator::ComputeCurl(const GraphicsVolume3& vorticity,
                                 const GraphicsVolume3& velocity,
                                 float cell_size)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ComputeCurl(vorticity.x()->cuda_volume(),
                                          vorticity.y()->cuda_volume(),
                                          vorticity.z()->cuda_volume(),
                                          velocity.x()->cuda_volume(),
                                          velocity.y()->cuda_volume(),
                                          velocity.z()->cuda_volume(),
                                          cell_size);
        if (diagnosis_ == DIAG_CURL) {
            CudaMain::Instance()->PrintVolume(vorticity.x()->cuda_volume(),
                                              "CurlX");
            CudaMain::Instance()->PrintVolume(vorticity.y()->cuda_volume(),
                                              "CurlY");
            CudaMain::Instance()->PrintVolume(vorticity.z()->cuda_volume(),
                                              "CurlZ");
        }
    }

}

void FluidSimulator::ComputeDeltaVorticity(const GraphicsVolume3& aux,
                                           const GraphicsVolume3& vorticity)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ComputeDeltaVorticity(
            aux.x()->cuda_volume(), aux.y()->cuda_volume(),
            aux.z()->cuda_volume(), vorticity.x()->cuda_volume(),
            vorticity.y()->cuda_volume(), vorticity.z()->cuda_volume());

        if (diagnosis_ == DIAG_DELTA_VORT) {
            CudaMain::Instance()->PrintVolume(aux.x()->cuda_volume(),
                                              "DeltaVortX");
            CudaMain::Instance()->PrintVolume(aux.y()->cuda_volume(),
                                              "DeltaVortY");
            CudaMain::Instance()->PrintVolume(aux.z()->cuda_volume(),
                                              "DeltaVortZ");
        }
    }

}

void FluidSimulator::DecayVortices(const GraphicsVolume3& vorticity,
                                   std::shared_ptr<GraphicsVolume> aux,
                                   float delta_time, float cell_size)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->ComputeDivergence(
            aux->cuda_volume(), velocity_prime_.x()->cuda_volume(),
            velocity_prime_.y()->cuda_volume(),
            velocity_prime_.z()->cuda_volume(), cell_size);
        CudaMain::Instance()->DecayVortices(
            vorticity.x()->cuda_volume(), vorticity.y()->cuda_volume(),
            vorticity.z()->cuda_volume(), aux->cuda_volume(), delta_time);
    }
}

void FluidSimulator::RestoreVorticity(float delta_time, float cell_size)
{
    if (FluidConfig::Instance()->vorticity_confinement() > 0.0f) {
        const GraphicsVolume3& vorticity = GetVorticityField();
        if (!vorticity)
            return;

        ComputeCurl(vorticity, velocity_prime_, cell_size);
        BuildVorticityConfinemnet(delta_time, cell_size);

        GraphicsVolume3 temp(general1a_, general1b_, general1c_);
        StretchVortices(temp, vorticity, delta_time, cell_size);
        DecayVortices(temp, general1d_, delta_time, cell_size);

        //////////////////////////
        general1d_->Clear();
        //////////////////////////

        AdvectVortices(vorticity, temp, general1d_, cell_size, delta_time);

        //////////////////////////
        general1a_->Clear();
        general1b_->Clear();
        general1c_->Clear();
        //////////////////////////

        ComputeCurl(temp, velocity_, cell_size);
        ComputeDeltaVorticity(temp, vorticity);
        SolvePsi(vorticity, temp, cell_size);
        AddCurlPsi(vorticity, cell_size);

        ApplyVorticityConfinemnet();
    }
}

void FluidSimulator::SolvePsi(const GraphicsVolume3& psi,
                              const GraphicsVolume3& delta_vort,
                              float cell_size)
{
    if (!multigrid_core_) {
        if (graphics_lib_ == GRAPHICS_LIB_CUDA)
            multigrid_core_.reset(new MultigridCoreCuda());
        else
            multigrid_core_.reset(new MultigridCoreGlsl());
    }

    int num_multigrid_iterations =
        FluidConfig::Instance()->num_multigrid_iterations();
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        if (!psi_solver_) {
            psi_solver_.reset(
                new MultigridPoissonSolver(multigrid_core_.get()));
            psi_solver_->Initialize(psi.x()->GetWidth(), psi.x()->GetHeight(),
                                    psi.x()->GetDepth(), volume_byte_width_, 8);
        }

        for (int i = 0; i < psi.num_of_volumes(); i++) {
            psi[i]->Clear();
            for (int j = 0; j < num_multigrid_iterations; j++)
                pressure_solver_->Solve(psi[i], delta_vort[i], cell_size, !j);
        }

        if (diagnosis_ == DIAG_PSI) {
            CudaMain::Instance()->PrintVolume(psi.x()->cuda_volume(), "PsiX");
            CudaMain::Instance()->PrintVolume(psi.y()->cuda_volume(), "PsiY");
            CudaMain::Instance()->PrintVolume(psi.z()->cuda_volume(), "PsiZ");
        }
    }
}

void FluidSimulator::StretchVortices(const GraphicsVolume3& vort_np1,
                                     const GraphicsVolume3& vorticity,
                                     float delta_time, float cell_size)
{
    if (graphics_lib_ == GRAPHICS_LIB_CUDA) {
        CudaMain::Instance()->StretchVortices(
            vort_np1.x()->cuda_volume(), vort_np1.y()->cuda_volume(),
            vort_np1.z()->cuda_volume(), velocity_prime_.x()->cuda_volume(),
            velocity_prime_.y()->cuda_volume(),
            velocity_prime_.z()->cuda_volume(),
            vorticity.x()->cuda_volume(), vorticity.y()->cuda_volume(),
            vorticity.z()->cuda_volume(), cell_size, delta_time);
    }
}

const GraphicsVolume3& FluidSimulator::GetVorticityField()
{
    if (!vorticity_) {
        int width = static_cast<int>(grid_size_.x);
        int height = static_cast<int>(grid_size_.y);
        int depth = static_cast<int>(grid_size_.z);
        bool r = vorticity_.Create(width, height, depth, 1, 2, 0);
        assert(r);
    }

    return vorticity_;
}

const GraphicsVolume3& FluidSimulator::GetAuxField()
{
    if (!aux_) {
        int width = static_cast<int>(grid_size_.x);
        int height = static_cast<int>(grid_size_.y);
        int depth = static_cast<int>(grid_size_.z);
        bool r = aux_.Create(width, height, depth, 1, 2, 0);
        assert(r);
    }

    return aux_;
}

const GraphicsVolume3& FluidSimulator::GetVorticityConfinementField()
{
    if (!vort_conf_) {
        int width = static_cast<int>(grid_size_.x);
        int height = static_cast<int>(grid_size_.y);
        int depth = static_cast<int>(grid_size_.z);
        bool r = vort_conf_.Create(width, height, depth, 1, 2, 0);
        assert(r);
    }

    return vort_conf_;
}
