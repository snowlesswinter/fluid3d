#ifndef _PRECONDITIONED_CONJUGATE_GRADIENT_H_
#define _PRECONDITIONED_CONJUGATE_GRADIENT_H_

#include <memory>
#include <vector>

#include "poisson_solver.h"

class GraphicsMemPiece;
class MultigridCore;
class MultigridPoissonSolver;
class PreconditionedConjugateGradient : public PoissonSolver
{
public:
    explicit PreconditionedConjugateGradient(MultigridCore* core);
    virtual ~PreconditionedConjugateGradient();

    virtual bool Initialize(int width, int height, int depth,
                            int byte_width, int minimum_grid_width) override;
    virtual void Solve(std::shared_ptr<GraphicsVolume> u,
                       std::shared_ptr<GraphicsVolume> b, float cell_size,
                       int iteration_times) override;

private:
    MultigridCore* core_;
    std::unique_ptr<MultigridPoissonSolver> preconditioner_;
    std::shared_ptr<GraphicsMemPiece> alpha_;
    std::shared_ptr<GraphicsMemPiece> beta_;
    std::shared_ptr<GraphicsMemPiece> rho_;
    std::shared_ptr<GraphicsMemPiece> rho_new_;
    std::shared_ptr<GraphicsVolume> residual_;
    std::shared_ptr<GraphicsVolume> aux_;
    std::shared_ptr<GraphicsVolume> search_;
};

#endif // _PRECONDITIONED_CONJUGATE_GRADIENT_H_