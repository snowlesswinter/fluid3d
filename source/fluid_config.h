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

#ifndef _FLUID_CONFIG_H_
#define _FLUID_CONFIG_H_

#include <string>

#include "cuda_host/cuda_main.h"
#include "graphics_lib_enum.h"
#include "fluid_simulator.h"
#include "renderer/rendering.h"
#include "third_party/glm/vec3.hpp"

class FluidConfig
{
public:
    template <typename T>
    struct ConfigField
    {
        T value_;
        const char* desc_;

        ConfigField(const T& v, const char* d) : value_(v), desc_(d) {}
    };

    static FluidConfig* Instance();

    void CreateIfNeeded(const std::string& path);
    void Load(const std::string& path, const std::string& preset_path);
    void Reload();

    GraphicsLib graphics_lib() const { return graphics_lib_.value_; }
    PoissonSolverEnum poisson_method() const {
        return poisson_method_.value_;
    }
    CudaMain::AdvectionMethod advection_method() const {
        return advection_method_.value_;
    }
    CudaMain::FluidImpulse fluid_impluse() const {
        return fluid_impluse_.value_;
    }
    RenderMode render_mode() const { return render_mode_.value_; }
    glm::vec3 light_color() const { return light_color_.value_; }
    glm::vec3 light_position() const { return light_position_.value_; }
    glm::vec3 grid_size() const { return grid_size_.value_; }
    glm::vec3 emit_position() const { return emit_position_.value_; }
    float domain_size() const { return domain_size_.value_; }
    float ambient_temperature() const { return ambient_temperature_.value_; }
    float impulse_temperature() const { return impulse_temperature_.value_; }
    float impulse_density() const { return impulse_density_.value_; }
    float impulse_velocity() const { return impulse_velocity_.value_; }
    float smoke_buoyancy() const { return smoke_buoyancy_.value_; }
    float smoke_weight() const { return smoke_weight_.value_; }
    float temperature_dissipation() const {
        return temperature_dissipation_.value_;
    }
    float velocity_dissipation() const { return velocity_dissipation_.value_; }
    float density_dissipation() const { return density_dissipation_.value_; }
    float splat_radius_factor() const { return splat_radius_factor_.value_; }
    float fixed_time_step() const { return fixed_time_step_.value_; }
    float light_intensity() const { return light_intensity_.value_; }
    float light_absorption() const { return light_absorption_.value_; }
    float raycast_density_factor() const {
        return raycast_density_factor_.value_;
    }
    float raycast_occlusion_factor() const {
        return raycast_occlusion_factor_.value_;
    }
    float field_of_view() const { return field_of_view_.value_; }
    float time_stretch() const { return time_stretch_.value_; }
    int num_jacobi_iterations() const { return num_jacobi_iterations_.value_; }
    int num_multigrid_iterations() const {
        return num_multigrid_iterations_.value_;
    }
    int num_full_multigrid_iterations() const {
        return num_full_multigrid_iterations_.value_;
    }
    int num_mgpcg_iterations() const {
        return num_mgpcg_iterations_.value_;
    }
    bool auto_impulse() const { return !!auto_impulse_.value_; }
    bool staggered() const { return !!staggered_.value_; }
    bool mid_point() const { return !!mid_point_.value_; }
    bool outflow() const { return !!outflow_.value_; }
    float vorticity_confinement() const {
        return vorticity_confinement_.value_;
    }
    int num_raycast_samples() const { return num_raycast_samples_.value_; }
    int num_raycast_light_samples() const {
        return num_raycast_light_samples_.value_;
    }
    int max_num_particles() const { return max_num_particles_.value_; }
    int initial_viewport_width() const { return initial_viewport_width_; }

private:
    FluidConfig();
    ~FluidConfig();

    void Load(const std::string& file_path);
    void Parse(const std::string& key, const std::string& value);
    void Store(std::ostream& stream);

    std::string file_path_;
    std::string preset_path_;
    ConfigField<std::string> preset_file_;
    ConfigField<GraphicsLib> graphics_lib_;
    ConfigField<PoissonSolverEnum> poisson_method_;
    ConfigField<CudaMain::AdvectionMethod> advection_method_;
    ConfigField<CudaMain::FluidImpulse> fluid_impluse_;
    ConfigField<RenderMode> render_mode_;
    ConfigField<glm::vec3> light_color_;
    ConfigField<glm::vec3> light_position_;
    ConfigField<glm::vec3> grid_size_;
    ConfigField<glm::vec3> emit_position_;
    ConfigField<float> cell_size_;   // Obsolete
    ConfigField<float> domain_size_; // In meters.
    ConfigField<float> ambient_temperature_;
    ConfigField<float> impulse_temperature_;
    ConfigField<float> impulse_density_;
    ConfigField<float> impulse_velocity_;
    ConfigField<float> smoke_buoyancy_;
    ConfigField<float> smoke_weight_;
    ConfigField<float> temperature_dissipation_;
    ConfigField<float> velocity_dissipation_;
    ConfigField<float> density_dissipation_;
    ConfigField<float> splat_radius_factor_;
    ConfigField<float> fixed_time_step_;
    ConfigField<float> light_intensity_;
    ConfigField<float> light_absorption_;
    ConfigField<float> raycast_density_factor_;
    ConfigField<float> raycast_occlusion_factor_;
    ConfigField<float> field_of_view_;
    ConfigField<float> time_stretch_;
    ConfigField<float> vorticity_confinement_;
    ConfigField<int> num_jacobi_iterations_;
    ConfigField<int> num_multigrid_iterations_;
    ConfigField<int> num_full_multigrid_iterations_;
    ConfigField<int> num_mgpcg_iterations_;
    ConfigField<int> auto_impulse_;
    ConfigField<int> staggered_;
    ConfigField<int> mid_point_;
    ConfigField<int> outflow_;
    ConfigField<int> num_raycast_samples_;
    ConfigField<int> num_raycast_light_samples_;
    ConfigField<int> max_num_particles_;
    int initial_viewport_width_;
};

#endif // _FLUID_CONFIG_H_