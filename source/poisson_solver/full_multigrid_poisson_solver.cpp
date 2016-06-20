#include "stdafx.h"
#include "full_multigrid_poisson_solver.h"

#include <cassert>
#include <algorithm>

#include "graphics_volume.h"
#include "multigrid_core.h"
#include "multigrid_poisson_solver.h"
#include "utility.h"

const int kWidthOfCoarsestLevel = 32;

FullMultigridPoissonSolver::FullMultigridPoissonSolver(PoissonCore* core)
    : core_(core)
    , solver_(new MultigridPoissonSolver(core))
    , volume_resource_()
    , num_nested_iterations_(2)
{

}

FullMultigridPoissonSolver::~FullMultigridPoissonSolver()
{

}

bool FullMultigridPoissonSolver::Initialize(int width, int height, int depth,
                                            int byte_width,
                                            int minimum_grid_width)
{
    if (!solver_->Initialize(width, height, depth, byte_width,
                             minimum_grid_width))
        return false;

    // Placeholder for the solution buffer.
    volume_resource_.clear();
    volume_resource_.push_back(VolumePair());

    int min_width = std::min(std::min(width, height), depth);

    int scale = 2;
    while (min_width / scale > minimum_grid_width - 1) {
        int w = width / scale;
        int h = height / scale;
        int d = depth / scale;

        std::shared_ptr<GraphicsVolume> v0 = core_->CreateVolume(w, h, d, 1,
                                                                 byte_width);
        if (!v0)
            return false;

        std::shared_ptr<GraphicsVolume> v1 = core_->CreateVolume(w, h, d, 1,
                                                                 byte_width);
        if (!v1)
            return false;

        volume_resource_.push_back(std::make_pair(v0, v1));

        scale <<= 1;
    }

    return true;
}

void FullMultigridPoissonSolver::SetAuxiliaryVolumes(
    const std::vector<std::shared_ptr<GraphicsVolume>>& volumes)
{

}

void FullMultigridPoissonSolver::SetDiagnosis(bool diagnosis)
{

}

void FullMultigridPoissonSolver::SetNestedSolverIterations(int num_iterations)
{
    num_nested_iterations_ = num_iterations;
}

void FullMultigridPoissonSolver::Solve(std::shared_ptr<GraphicsVolume> u,
                                       std::shared_ptr<GraphicsVolume> b,
                                       float cell_size, int iteration_times)
{
    if (u->GetWidth() < 32) {
            solver_->Solve(u, b, cell_size, true);
        return;
    }

    for (int i = 0; i < iteration_times; i++)
        Iterate(u, b, cell_size, !i);
}

void FullMultigridPoissonSolver::Iterate(std::shared_ptr<GraphicsVolume> u,
                                         std::shared_ptr<GraphicsVolume> b,
                                         float cell_size,
                                         bool apply_initial_guess)
{
    assert(volume_resource_.size() > 1);
    if (volume_resource_.size() <= 1)
        return;

    // With less iterations in each level but more iterating in every V-Cycle
    // will out perform the case visa versa(less time cost, lower avg/max |r|),
    // especially in high divergence cases.
    solver_->set_num_finest_level_iteration_per_pass(num_nested_iterations_);
    volume_resource_[0] = std::make_pair(u, b);

    const int num_of_levels = static_cast<int>(volume_resource_.size());
    float level_cell_size = cell_size;
    for (int i = 0; i < num_of_levels - 1; i++) {
        VolumePair fine_volume = volume_resource_[i];
        VolumePair coarse_volume = volume_resource_[i + 1];

        if (!i && apply_initial_guess)
            core_->RelaxWithZeroGuess(*fine_volume.first, *fine_volume.second,
                                      level_cell_size);
        else
            core_->Relax(*fine_volume.first, *fine_volume.second,
                         level_cell_size, 1);

        core_->Restrict(*coarse_volume.first, *fine_volume.first);

        if (apply_initial_guess)
            core_->Restrict(*coarse_volume.second, *fine_volume.second);

        level_cell_size *= 2.0f;
    }

    VolumePair coarsest = volume_resource_[num_of_levels - 1];
    //if (as_precondition)
    //    core_->RelaxWithZeroGuess(*coarsest.first, *coarsest.second, level_cell_size);

    core_->Relax(*coarsest.first, *coarsest.second, level_cell_size, 16);

    int times_to_iterate = 1;
    for (int j = num_of_levels - 2; j >= 0; j--) {
        VolumePair coarse_volume = volume_resource_[j + 1];
        VolumePair fine_volume = volume_resource_[j];

        level_cell_size *= 0.5f;

        core_->Prolongate(*fine_volume.first, *coarse_volume.first);

        solver_->Solve(fine_volume.first, fine_volume.second, level_cell_size,
                       times_to_iterate);

        // For comparison.
        // 
        // Damped Jacobi is still faster than Multigrid, not much though. With
        // a base relaxation times of 5, Multigrid had achieved a notable
        // lower avg/max |r| compared to Jacobi in our experiments.

        //core_->Relax(*fine_volume.first, *fine_volume.second, level_cell_size, 15);

        // Experiments revealed that iterations in different levels almost
        // equally contribute to the final result, thus we are not going to
        // reduce the iteration times in coarsen level.
        times_to_iterate += 0;
    }
}
