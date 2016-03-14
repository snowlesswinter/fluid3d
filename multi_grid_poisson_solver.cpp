#include "stdafx.h"
#include "multi_grid_poisson_solver.h"

#include <cassert>
#include <tuple>

#include "gl_program.h"
#include "utility.h"
#include "fluid_shader.h"

MultiGridPoissonSolver::MultiGridPoissonSolver()
    : multi_grid_surfaces_()
    , restricted_residual_()
    , temp_surface_()
    , residual_program_()
    , restrict_program_()
    , prolongate_program_()
{
}

MultiGridPoissonSolver::~MultiGridPoissonSolver()
{

}

void MultiGridPoissonSolver::Initialize(int grid_width)
{
    assert(!multi_grid_surfaces_);
    multi_grid_surfaces_.reset(new MultiGridSurfaces());

    // Placeholder for the solution buffer.
    multi_grid_surfaces_->push_back(
        std::make_tuple(SurfacePod(), SurfacePod(), SurfacePod()));

    int width = grid_width >> 1;
    while (width >= 8) {
        multi_grid_surfaces_->push_back(
            std::make_tuple(
                CreateVolume(width, width, width, 1),
                CreateVolume(width, width, width, 1),
                CreateVolume(width, width, width, 1)));

        width >>= 1;
    }

//     restricted_residual_.reset(
//         new SurfacePod(
//             CreateVolume(grid_width >> 1, grid_width >> 1, grid_width >> 1,
//                          1)));

    temp_surface_.reset(
        new SurfacePod(
            CreateVolume(grid_width, grid_width, grid_width, 1)));

    residual_program_.reset(new GLProgram());
    residual_program_->Load(FluidShader::GetVertexShaderCode(),
                            FluidShader::GetPickLayerShaderCode(),
                            FluidShader::GetComputeResidualShaderCode());
    restrict_program_.reset(new GLProgram());
    restrict_program_->Load(FluidShader::GetVertexShaderCode(),
                            FluidShader::GetPickLayerShaderCode(),
                            FluidShader::GetRestrictShaderCode());
    prolongate_program_.reset(new GLProgram());
    prolongate_program_->Load(FluidShader::GetVertexShaderCode(),
                              FluidShader::GetPickLayerShaderCode(),
                              FluidShader::GetProlongateShaderCode());
}

void MultiGridPoissonSolver::Solve(const SurfacePod& pressure,
                                   const SurfacePod& divergence)
{
    assert(multi_grid_surfaces_);
    assert(multi_grid_surfaces_->size() > 1);
//     assert(restricted_residual_);
//     if (!multi_grid_surfaces_ || multi_grid_surfaces_->empty() ||
//             !restricted_residual_)
//         return;
    if (!multi_grid_surfaces_ || multi_grid_surfaces_->empty())
        return;

    const int kRelaxIteration = 10;
    (*multi_grid_surfaces_)[0] = std::make_tuple(pressure, divergence,
                                                 *temp_surface_);

    const int num_of_levels = static_cast<int>(multi_grid_surfaces_->size());
    for (int i = 0; i < num_of_levels - 1; i++)
    {
        Surface& fine_surf = (*multi_grid_surfaces_)[i];
        Surface& coarse_surf = (*multi_grid_surfaces_)[i + 1];

        ClearSurface(std::get<0>(fine_surf), 0.0f);
        Relax(std::get<0>(fine_surf), std::get<1>(fine_surf), kRelaxIteration);

        ComputeResidual(std::get<0>(fine_surf), std::get<1>(fine_surf),
                        std::get<2>(fine_surf));
        Restrict(std::get<2>(fine_surf), std::get<1>(coarse_surf));
    }

    Surface coarsest = (*multi_grid_surfaces_)[num_of_levels - 1];
    ClearSurface(std::get<0>(coarsest), 0.0f);
    Relax(std::get<0>(coarsest), std::get<1>(coarsest), 30);

    for (int j = num_of_levels - 2; j >= 0; j--)
    {
        Surface& coarse_surf = (*multi_grid_surfaces_)[j + 1];
        Surface& fine_surf = (*multi_grid_surfaces_)[j];
        
        Prolongate(std::get<0>(coarse_surf), std::get<0>(fine_surf));
        Relax(std::get<0>(fine_surf), std::get<1>(fine_surf), kRelaxIteration);
    }
}

void MultiGridPoissonSolver::ComputeResidual(const SurfacePod& pressure,
                                             const SurfacePod& divergence,
                                             const SurfacePod& residual)
{
    assert(residual_program_);
    if (!residual_program_)
        return;

    residual_program_->Use();

    SetUniform("residual", 0);
    SetUniform("pressure", 1);
    SetUniform("divergence", 2);
    SetUniform("inverse_h_square", 1.0f / (CellSize * CellSize));

    glBindFramebuffer(GL_FRAMEBUFFER, residual.FboHandle);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, residual.ColorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, pressure.ColorTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, divergence.ColorTexture);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, residual.Depth);
    ResetState();
}

void MultiGridPoissonSolver::Prolongate(const SurfacePod& coarse_solution,
                                        const SurfacePod& fine_solution)
{
    assert(prolongate_program_);
    if (!prolongate_program_)
        return;

    prolongate_program_->Use();

    SetUniform("fine", 0);
    SetUniform("c", 1);

    glBindFramebuffer(GL_FRAMEBUFFER, fine_solution.FboHandle);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, fine_solution.ColorTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, coarse_solution.ColorTexture);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, fine_solution.Depth);
    ResetState();
}

void MultiGridPoissonSolver::Relax(const SurfacePod& u, const SurfacePod& b,
                                   int times)
{
    for (int i = 0; i < times; i++)
        DampedJacobi(u, b, SurfacePod());
}

void MultiGridPoissonSolver::Restrict(const SurfacePod& source,
                                      const SurfacePod& dest)
{
    assert(restrict_program_);
    if (!restrict_program_)
        return;

    restrict_program_->Use();

    SetUniform("s", 0);

    glBindFramebuffer(GL_FRAMEBUFFER, dest.FboHandle);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, source.ColorTexture);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, dest.Depth);
    ResetState();
}