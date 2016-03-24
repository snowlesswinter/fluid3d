#ifndef _MULTIGRID_POISSON_SOLVER_H_
#define _MULTIGRID_POISSON_SOLVER_H_

#include <memory>
#include <vector>

#include "poisson_solver.h"

class FullMultigridPoissonSolver;
class GLProgram;
class GLTexture;
class MultigridCore;
class MultigridPoissonSolver : public PoissonSolver
{
public:
    MultigridPoissonSolver();
    virtual ~MultigridPoissonSolver();

    virtual void Initialize(int width, int height, int depth) override;
    virtual void Solve(std::shared_ptr<GLTexture> u_and_b, float cell_size,
                       bool as_precondition) override;

    // TODO
    void Diagnose(GLTexture* packed);

private:
    friend class FullMultigridPoissonSolver;

    typedef std::vector<std::tuple<std::shared_ptr<GLTexture>, std::shared_ptr<GLTexture>, std::shared_ptr<GLTexture>>>
        MultigridSurfaces;
    typedef MultigridSurfaces::value_type Surface;

    void ComputeResidual(std::shared_ptr<GLTexture> u,
                         std::shared_ptr<GLTexture> b,
                         std::shared_ptr<GLTexture> residual, float cell_size,
                         bool diagnosis);
    void Prolongate(std::shared_ptr<GLTexture> coarse_solution,
                    std::shared_ptr<GLTexture> fine_solution);
    void Relax(std::shared_ptr<GLTexture> u, const std::shared_ptr<GLTexture> b, float cell_size,
               int times);
    void RelaxWithZeroGuess(std::shared_ptr<GLTexture> u, std::shared_ptr<GLTexture> b,
                            float cell_size);
    void Restrict(std::shared_ptr<GLTexture> fine, std::shared_ptr<GLTexture> coarse);
    void SetBaseRelaxationTimes(int base_times);
    void SolvePlain(std::shared_ptr<GLTexture> u_and_b, float cell_size,
                    bool as_precondition);
    bool ValidateVolume(std::shared_ptr<GLTexture> u_and_b);

    MultigridCore* core() const;

    // Optimization.
    void ComputeResidualPacked(std::shared_ptr<GLTexture> packed, float cell_size);
    void ProlongateAndRelax(std::shared_ptr<GLTexture> coarse, std::shared_ptr<GLTexture> fine);
    void ProlongatePacked(std::shared_ptr<GLTexture> coarse,
                          std::shared_ptr<GLTexture> fine);
    void RelaxPacked(std::shared_ptr<GLTexture> u_and_b, float cell_size, int times);
    void RelaxPackedImpl(std::shared_ptr<GLTexture> u_and_b, float cell_size);
    void RelaxWithZeroGuessAndComputeResidual(std::shared_ptr<GLTexture> packed_volumes,
                                              float cell_size, int times);
    void RelaxWithZeroGuessPacked(std::shared_ptr<GLTexture> packed_volumes,
                                  float cell_size);
    void RestrictPacked(std::shared_ptr<GLTexture> fine, std::shared_ptr<GLTexture> coarse);
    void SolveOpt(std::shared_ptr<GLTexture> u_and_b, float cell_size,
                  bool as_precondition);

    // For diagnosis.
    void ProlongatePacked2(std::shared_ptr<GLTexture> coarse,
                           std::shared_ptr<GLTexture> fine);
    void ComputeResidualPackedDiagnosis(const GLTexture& packed,
                                        const GLTexture& diagnosis,
                                        float cell_size);

    std::unique_ptr<MultigridCore> core_;
    std::unique_ptr<MultigridSurfaces> multi_grid_surfaces_;
    std::vector<std::shared_ptr<GLTexture>> surf_resource;
    std::unique_ptr<std::shared_ptr<GLTexture>> temp_surface_; // TODO
    std::unique_ptr<GLProgram> residual_program_;
    std::unique_ptr<GLProgram> restrict_program_;
    std::unique_ptr<GLProgram> prolongate_program_;
    std::unique_ptr<GLProgram> relax_zero_guess_program_;
    int times_to_iterate_;
    bool diagnosis_;

    // Optimization.
    std::unique_ptr<GLProgram> prolongate_and_relax_program_;
    std::unique_ptr<GLProgram> prolongate_packed_program_;
    std::unique_ptr<GLProgram> relax_packed_program_;
    std::unique_ptr<GLProgram> relax_zero_guess_packed_program_;
    std::unique_ptr<GLProgram> residual_packed_program_;
    std::unique_ptr<GLProgram> restrict_packed_program_;

    // For diagnosis.
    std::unique_ptr<GLProgram> absolute_program_; 
    std::unique_ptr<GLProgram> residual_diagnosis_program_;
    std::shared_ptr<GLTexture> diagnosis_volume_;
};

#endif // _MULTIGRID_POISSON_SOLVER_H_