#include "stdafx.h"
#include "multigrid_core_glsl.h"

#include <cassert>

#include "cuda_host/cuda_main.h"
#include "graphics_volume.h"
#include "opengl/gl_program.h"
#include "opengl/gl_texture.h"
#include "shader/fluid_shader.h"
#include "shader/multigrid_shader.h"
#include "shader/multigrid_staggered_shader.h"
#include "utility.h"

MultigridCoreGlsl::MultigridCoreGlsl()
    : MultigridCore()
    , prolongate_and_relax_program_()
    , prolongate_packed_program_()
    , relax_packed_program_()
    , relax_zero_guess_packed_program_()
    , residual_packed_program_()
    , restrict_packed_program_()
    , restrict_residual_packed_program_()
{

}

MultigridCoreGlsl::~MultigridCoreGlsl()
{

}

std::shared_ptr<GraphicsVolume> MultigridCoreGlsl::CreateVolume(
    int width, int height, int depth, int num_of_components, int byte_width)
{
    std::shared_ptr<GraphicsVolume> r(new GraphicsVolume(GRAPHICS_LIB_GLSL));
    bool succeeded = r->Create(width, height, depth, num_of_components,
                               byte_width);

    return succeeded ? r : std::shared_ptr<GraphicsVolume>();
}

void MultigridCoreGlsl::ComputeResidualPacked(const GraphicsVolume& packed,
                                              float cell_size)
{
    GetResidualPackedProgram()->Use();

    SetUniform("packed_tex", 0);
    SetUniform("inverse_h_square", 1.0f / (cell_size * cell_size));

    glBindFramebuffer(GL_FRAMEBUFFER, packed.gl_texture()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, packed.gl_texture()->handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          packed.gl_texture()->depth());
    ResetState();
}

void MultigridCoreGlsl::ProlongatePacked(const GraphicsVolume& coarse,
                                         const GraphicsVolume& fine)
{
    GetProlongatePackedProgram()->Use();

    SetUniform("fine", 0);
    SetUniform("s", 1);
    SetUniform("inverse_size_f", CalculateInverseSize(*fine.gl_texture()));
    SetUniform("inverse_size_c", CalculateInverseSize(*coarse.gl_texture()));

    glBindFramebuffer(GL_FRAMEBUFFER, fine.gl_texture()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, fine.gl_texture()->handle());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, coarse.gl_texture()->handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          fine.gl_texture()->depth());
    ResetState();
}

void MultigridCoreGlsl::RelaxPacked(const GraphicsVolume& u_and_b,
                                    float cell_size)
{
    GetRelaxPackedProgram()->Use();

    SetUniform("packed_tex", 0);
    SetUniform("one_minus_omega", 0.33333333f);
    SetUniform("minus_h_square", -(cell_size * cell_size));
    SetUniform("omega_over_beta", 0.11111111f);

    glBindFramebuffer(GL_FRAMEBUFFER, u_and_b.gl_texture()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, u_and_b.gl_texture()->handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          u_and_b.gl_texture()->depth());
    ResetState();
}

void MultigridCoreGlsl::RelaxWithZeroGuessAndComputeResidual(
    const GraphicsVolume& packed_volumes, float cell_size, int times)
{
    // Just wait and see how the profiler tells us.
}

void MultigridCoreGlsl::RelaxWithZeroGuessPacked(const GraphicsVolume& packed,
                                                 float cell_size)
{
    GetRelaxZeroGuessPackedProgram()->Use();

    SetUniform("packed_tex", 0);
    SetUniform("alpha_omega_over_beta",
               -(cell_size * cell_size) * 0.11111111f);
    SetUniform("one_minus_omega", 0.33333333f);
    SetUniform("minus_h_square", -(cell_size * cell_size));
    SetUniform("omega_times_inverse_beta", 0.11111111f);

    glBindFramebuffer(GL_FRAMEBUFFER, packed.gl_texture()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, packed.gl_texture()->handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          packed.gl_texture()->depth());
    ResetState();
}

void MultigridCoreGlsl::RestrictPacked(const GraphicsVolume& fine,
                                       const GraphicsVolume& coarse)
{
    GetRestrictPackedProgram()->Use();

    SetUniform("s", 0);
    SetUniform("inverse_size", CalculateInverseSize(*fine.gl_texture()));

    glBindFramebuffer(GL_FRAMEBUFFER, coarse.gl_texture()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, fine.gl_texture()->handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          coarse.gl_texture()->depth());
    ResetState();
}

void MultigridCoreGlsl::RestrictResidualPacked(const GraphicsVolume& fine,
                                               const GraphicsVolume& coarse)
{
    GetRestrictResidualPackedProgram()->Use();

    SetUniform("s", 0);
    SetUniform("inverse_size", CalculateInverseSize(*fine.gl_texture()));

    glBindFramebuffer(GL_FRAMEBUFFER, coarse.gl_texture()->frame_buffer());
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, fine.gl_texture()->handle());
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4,
                          coarse.gl_texture()->depth());
    ResetState();
}

GLProgram* MultigridCoreGlsl::GetProlongatePackedProgram()
{
    if (!prolongate_packed_program_)
    {
        prolongate_packed_program_.reset(new GLProgram());
        prolongate_packed_program_->Load(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridStaggeredShader::ProlongatePacked());
    }

    return prolongate_packed_program_.get();
}

GLProgram* MultigridCoreGlsl::GetRelaxPackedProgram()
{
    if (!relax_packed_program_)
    {
        relax_packed_program_.reset(new GLProgram());
        relax_packed_program_->Load(FluidShader::Vertex(),
                                    FluidShader::PickLayer(),
                                    MultigridShader::RelaxPacked());
    }

    return relax_packed_program_.get();
}

GLProgram* MultigridCoreGlsl::GetRelaxZeroGuessPackedProgram()
{
    if (!relax_zero_guess_packed_program_)
    {
        relax_zero_guess_packed_program_.reset(new GLProgram());
        relax_zero_guess_packed_program_->Load(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridShader::RelaxWithZeroGuessPacked());
    }

    return relax_zero_guess_packed_program_.get();
}

GLProgram* MultigridCoreGlsl::GetResidualPackedProgram()
{
    if (!residual_packed_program_)
    {
        residual_packed_program_.reset(new GLProgram());
        residual_packed_program_->Load(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridShader::ComputeResidualPacked());
    }

    return residual_packed_program_.get();
}

GLProgram* MultigridCoreGlsl::GetRestrictPackedProgram()
{
    if (!restrict_packed_program_)
    {
        restrict_packed_program_.reset(new GLProgram());
        restrict_packed_program_->Load(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridStaggeredShader::RestrictPacked());
    }

    return restrict_packed_program_.get();
}

GLProgram* MultigridCoreGlsl::GetRestrictResidualPackedProgram()
{
    if (!restrict_residual_packed_program_)
    {
        restrict_residual_packed_program_.reset(new GLProgram());
        restrict_residual_packed_program_->Load(
            FluidShader::Vertex(), FluidShader::PickLayer(),
            MultigridStaggeredShader::RestrictResidualPacked());
    }

    return restrict_residual_packed_program_.get();
}