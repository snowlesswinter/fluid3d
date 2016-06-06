#include <cassert>

#include "third_party/opengl/glew.h"

#include <helper_math.h>

#include "block_arrangement.h"
#include "cuda_common.h"

surface<void, cudaSurfaceType3D> surf;
surface<void, cudaSurfaceType3D> surf_x;
surface<void, cudaSurfaceType3D> surf_y;
surface<void, cudaSurfaceType3D> surf_z;
surface<void, cudaSurfaceType3D> buoyancy_dest;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_x;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_y;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_z;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_b;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_t;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_d;
texture<ushort4, cudaTextureType3D, cudaReadModeNormalizedFloat> buoyancy_velocity;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> buoyancy_temperature;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> buoyancy_density;
surface<void, cudaSurfaceType3D> impulse_dest1;
surface<void, cudaSurfaceType3D> impulse_dest4;
texture<ushort4, cudaTextureType3D, cudaReadModeNormalizedFloat> divergence_velocity;
texture<ushort4, cudaTextureType3D, cudaReadModeNormalizedFloat> gradient_velocity;

__global__ void ApplyBuoyancyKernel(float time_step, float ambient_temperature,
                                    float accel_factor, float gravity)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z) + 0.5f;
    float t = tex3D(buoyancy_temperature, coord.x, coord.y, coord.z);
    float d = tex3D(buoyancy_density, coord.x, coord.y, coord.z);
    float accel =
        time_step * ((t - ambient_temperature) * accel_factor - d * gravity);

    float4 velocity = tex3D(buoyancy_velocity, coord.x, coord.y, coord.z);
    ushort4 result = make_ushort4(__float2half_rn(velocity.x),
                                  __float2half_rn(velocity.y + accel),
                                  __float2half_rn(velocity.z),
                                  0);
    surf3Dwrite(result, buoyancy_dest, x * sizeof(ushort4), y, z,
                cudaBoundaryModeTrap);
}

__global__ void ApplyBuoyancyStaggeredKernel(float time_step,
                                             float ambient_temperature,
                                             float accel_factor, float gravity,
                                             float3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z) + 0.5f;
    float t = tex3D(buoyancy_temperature, coord.x, coord.y, coord.z);
    float d = tex3D(buoyancy_density, coord.x, coord.y, coord.z);
    float accel =
        time_step * ((t - ambient_temperature) * accel_factor - d * gravity);

    float4 velocity;
    ushort4 result;
    if (y > 0) {
        velocity = tex3D(buoyancy_velocity, coord.x, coord.y, coord.z);
        result = make_ushort4(__float2half_rn(velocity.x),
                              __float2half_rn(velocity.y + accel * 0.5f),
                              __float2half_rn(velocity.z),
                              0);
        surf3Dwrite(result, buoyancy_dest, x * sizeof(ushort4), y, z,
                    cudaBoundaryModeTrap);
    }
    if (y < volume_size.y - 1) {
        velocity = tex3D(buoyancy_velocity, coord.x, coord.y + 1.0f, coord.z);
        result = make_ushort4(__float2half_rn(velocity.x),
                              __float2half_rn(velocity.y + accel * 0.5f),
                              __float2half_rn(velocity.z),
                              0);
        surf3Dwrite(result, buoyancy_dest, x * sizeof(ushort4), y + 1, z,
                    cudaBoundaryModeTrap);
    }
}

__global__ void ComputeDivergenceKernel(float half_inverse_cell_size,
                                        uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z); // Careful: Non-interpolation version.

    float  near =   tex3D(divergence_velocity, coord.x,        coord.y,        coord.z - 1.0f).z;
    float  south =  tex3D(divergence_velocity, coord.x,        coord.y - 1.0f, coord.z).y;
    float  west =   tex3D(divergence_velocity, coord.x - 1.0f, coord.y,        coord.z).x;
    float4 center = tex3D(divergence_velocity, coord.x,        coord.y,        coord.z);
    float  east =   tex3D(divergence_velocity, coord.x + 1.0f, coord.y,        coord.z).x;
    float  north =  tex3D(divergence_velocity, coord.x,        coord.y + 1.0f, coord.z).y;
    float  far =    tex3D(divergence_velocity, coord.x,        coord.y,        coord.z + 1.0f).z;

    float diff_ew = east - west;
    float diff_ns = north - south;
    float diff_fn = far - near;

    // Handle boundary problem.
    if (x >= volume_size.x - 1)
        diff_ew = -center.x - west;

    if (x <= 0)
        diff_ew = east + center.x;

    if (y >= volume_size.y - 1)
        diff_ns = -center.y - south;

    if (y <= 0)
        diff_ns = north + center.y;

    if (z >= volume_size.z - 1)
        diff_fn = -center.z - near;

    if (z <= 0)
        diff_fn = far + center.z;

    float div = half_inverse_cell_size * (diff_ew + diff_ns + diff_fn);
    auto r = __float2half_rn(div);
    surf3Dwrite(r, surf, x * sizeof(r), y, z, cudaBoundaryModeTrap);
}

__global__ void ComputeDivergenceStaggeredKernel(float inverse_cell_size,
                                                 uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z) + 0.5f;

    float base_x = tex3D(tex_x, coord.x,        coord.y,        coord.z);
    float base_y = tex3D(tex_y, coord.x,        coord.y,        coord.z);
    float base_z = tex3D(tex_z, coord.x,        coord.y,        coord.z);
    float east =   tex3D(tex_x, coord.x + 1.0f, coord.y,        coord.z);
    float north =  tex3D(tex_y, coord.x,        coord.y + 1.0f, coord.z);
    float far =    tex3D(tex_z, coord.x,        coord.y,        coord.z + 1.0f);

    float diff_ew = east  - base_x;
    float diff_ns = north - base_y;
    float diff_fn = far   - base_z;

    // Handle boundary problem
    if (x >= volume_size.x - 1)
        diff_ew = -base_x;

    if (y >= volume_size.y - 1)
        diff_ns = -base_y;

    if (z >= volume_size.z - 1)
        diff_fn = -base_z;

    float div = inverse_cell_size * (diff_ew + diff_ns + diff_fn);
    auto r = __float2half_rn(div);
    surf3Dwrite(r, surf, x * sizeof(r), y, z, cudaBoundaryModeTrap);
}

__global__ void ComputeResidualDiagnosisKernel(float inverse_h_square,
                                               uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z);

    float near =   tex3D(tex, coord.x, coord.y, coord.z - 1.0f);
    float south =  tex3D(tex, coord.x, coord.y - 1.0f, coord.z);
    float west =   tex3D(tex, coord.x - 1.0f, coord.y, coord.z);
    float center = tex3D(tex, coord.x, coord.y, coord.z);
    float east =   tex3D(tex, coord.x + 1.0f, coord.y, coord.z);
    float north =  tex3D(tex, coord.x, coord.y + 1.0f, coord.z);
    float far =    tex3D(tex, coord.x, coord.y, coord.z + 1.0f);
    float b =      tex3D(tex_b, coord.x, coord.y, coord.z);

    if (coord.y == volume_size.y - 1)
        north = center;

    if (coord.y == 0)
        south = center;

    if (coord.x == volume_size.x - 1)
        east = center;

    if (coord.x == 0)
        west = center;

    if (coord.z == volume_size.z - 1)
        far = center;

    if (coord.z == 0)
        near = center;

    float v = b -
        (north + south + east + west + far + near - 6.0 * center) *
        inverse_h_square;
    surf3Dwrite(fabsf(v), surf, x * sizeof(float), y, z, cudaBoundaryModeTrap);
}

__global__ void RoundPassedKernel(int* dest_array, int round, int x)
{
    dest_array[0] = x * x - round * round;
}

__global__ void SubtractGradientKernel(float half_inverse_cell_size,
                                       uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z); // Careful: Non-interpolation version.

    float near =   tex3D(tex, coord.x, coord.y, coord.z - 1.0f);
    float south =  tex3D(tex, coord.x, coord.y - 1.0f, coord.z);
    float west =   tex3D(tex, coord.x - 1.0f, coord.y, coord.z);
    float center = tex3D(tex, coord.x, coord.y, coord.z);
    float east =   tex3D(tex, coord.x + 1.0f, coord.y, coord.z);
    float north =  tex3D(tex, coord.x, coord.y + 1.0f, coord.z);
    float far =    tex3D(tex, coord.x, coord.y, coord.z + 1.0f);

    float diff_ew = east - west;
    float diff_ns = north - south;
    float diff_fn = far - near;

    // Handle boundary problem
    float3 mask = make_float3(1.0f);
    if (x >= volume_size.x - 1) // Careful: Non-interpolation version.
        mask.x = 0.0f;

    if (x <= 0)
        mask.x = 0.0f;

    if (y >= volume_size.y - 1)
        mask.y = 0.0f;

    if (y <= 0)
        mask.y = 0.0f;

    if (z >= volume_size.z - 1)
        mask.z = 0.0f;

    if (z <= 0)
        mask.z = 0.0f;

    float3 old_v =
        make_float3(tex3D(gradient_velocity, coord.x, coord.y, coord.z));
    float3 grad =
        make_float3(diff_ew, diff_ns, diff_fn) * half_inverse_cell_size;
    float3 new_v = old_v - grad;
    float3 result = mask * new_v; // Velocity goes to 0 when hit ???
    ushort4 raw = make_ushort4(__float2half_rn(result.x),
                               __float2half_rn(result.y),
                               __float2half_rn(result.z),
                               0);
    surf3Dwrite(raw, surf, x * sizeof(ushort4), y, z,
                cudaBoundaryModeTrap);
}

__global__ void SubtractGradientStaggeredKernel(float inverse_cell_size,
                                                uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, y, z) + 0.5f;
    
    float near =  tex3D(tex, coord.x,        coord.y,          coord.z - 1.0f);
    float south = tex3D(tex, coord.x,        coord.y - 1.0f,   coord.z);
    float west =  tex3D(tex, coord.x - 1.0f, coord.y,          coord.z);
    float base =  tex3D(tex, coord.x,        coord.y,          coord.z);

    // Handle boundary problem.
    float mask = 1.0f;
    if (x <= 0)
        mask = 0;

    if (y <= 0)
        mask = 0;

    if (z <= 0)
        mask = 0;

    float old_x = tex3D(tex_x, coord.x, coord.y, coord.z);
    float grad_x = (base - west) * inverse_cell_size;
    float new_x = old_x - grad_x;
    auto r_x = __float2half_rn(new_x * mask);
    surf3Dwrite(r_x, surf, x * sizeof(r_x), y, z, cudaBoundaryModeTrap);

    float old_y = tex3D(tex_y, coord.x, coord.y, coord.z);
    float grad_y = (base - south) * inverse_cell_size;
    float new_y = old_y - grad_y;
    auto r_y = __float2half_rn(new_y * mask);
    surf3Dwrite(r_y, surf, x * sizeof(r_y), y, z, cudaBoundaryModeTrap);

    float old_z = tex3D(tex_z, coord.x, coord.y, coord.z);
    float grad_z = (base - near) * inverse_cell_size;
    float new_z = old_z - grad_z;
    auto r_z = __float2half_rn(new_z * mask);
    surf3Dwrite(r_z, surf, x * sizeof(r_z), y, z, cudaBoundaryModeTrap);
}

// =============================================================================

void LaunchApplyBuoyancy(cudaArray* dest_array, cudaArray* velocity_array,
                         cudaArray* temperature_array, cudaArray* density_array,
                         float time_step, float ambient_temperature,
                         float accel_factor, float gravity, uint3 volume_size)
{
    if (BindCudaSurfaceToArray(&buoyancy_dest, dest_array) != cudaSuccess)
        return;

    auto bound_vel = BindHelper::Bind(&buoyancy_velocity, velocity_array, false,
                                      cudaFilterModeLinear,
                                      cudaAddressModeClamp);
    if (bound_vel.error() != cudaSuccess)
        return;

    auto bound_temp = BindHelper::Bind(&buoyancy_temperature, temperature_array,
                                       false, cudaFilterModeLinear,
                                       cudaAddressModeClamp);
    if (bound_temp.error() != cudaSuccess)
        return;

    auto bound_density = BindHelper::Bind(&buoyancy_density, density_array,
                                          false, cudaFilterModeLinear,
                                          cudaAddressModeClamp);
    if (bound_density.error() != cudaSuccess)
        return;

    dim3 block(8, 8, 8);
    dim3 grid(volume_size.x / block.x, volume_size.y / block.y,
              volume_size.z / block.z);
    ApplyBuoyancyKernel<<<grid, block>>>(time_step, ambient_temperature,
                                         accel_factor, gravity);
}

void LaunchApplyBuoyancyStaggered(cudaArray* dest_array,
                                  cudaArray* velocity_array,
                                  cudaArray* temperature_array,
                                  cudaArray* density_array, float time_step,
                                  float ambient_temperature, float accel_factor,
                                  float gravity, uint3 volume_size)
{
    if (BindCudaSurfaceToArray(&buoyancy_dest, dest_array) != cudaSuccess)
        return;

    auto bound_vel = BindHelper::Bind(&buoyancy_velocity, velocity_array, false,
                                      cudaFilterModeLinear,
                                      cudaAddressModeClamp);
    if (bound_vel.error() != cudaSuccess)
        return;

    auto bound_temp = BindHelper::Bind(&buoyancy_temperature, temperature_array,
                                       false, cudaFilterModeLinear,
                                       cudaAddressModeClamp);
    if (bound_temp.error() != cudaSuccess)
        return;

    auto bound_density = BindHelper::Bind(&buoyancy_density, density_array,
                                          false, cudaFilterModeLinear,
                                          cudaAddressModeClamp);
    if (bound_density.error() != cudaSuccess)
        return;

    dim3 block(8, 8, 8);
    dim3 grid(volume_size.x / block.x, volume_size.y / block.y,
              volume_size.z / block.z);
    ApplyBuoyancyStaggeredKernel<<<grid, block>>>(time_step,
                                                  ambient_temperature,
                                                  accel_factor, gravity,
                                                  make_float3(volume_size));
}

void LaunchComputeDivergence(cudaArray* dest_array, cudaArray* velocity_array,
                             float half_inverse_cell_size, uint3 volume_size)
{
    if (BindCudaSurfaceToArray(&surf, dest_array) != cudaSuccess)
        return;

    auto bound_vel = BindHelper::Bind(&divergence_velocity, velocity_array,
                                      false, cudaFilterModePoint,
                                      cudaAddressModeClamp);
    if (bound_vel.error() != cudaSuccess)
        return;

    dim3 block(8, 8, 8);
    dim3 grid(volume_size.x / block.x, volume_size.y / block.y,
              volume_size.z / block.z);
    ComputeDivergenceKernel<<<grid, block>>>(half_inverse_cell_size,
                                             volume_size);
}

void LaunchComputeDivergenceStaggered(cudaArray* div, cudaArray* vel_x,
                                      cudaArray* vel_y, cudaArray* vel_z,
                                      float inverse_cell_size,
                                      uint3 volume_size)
{
    if (BindCudaSurfaceToArray(&surf, div) != cudaSuccess)
        return;

    auto bound_x = BindHelper::Bind(&tex_x, vel_x, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_x.error() != cudaSuccess)
        return;

    auto bound_y = BindHelper::Bind(&tex_y, vel_y, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_y.error() != cudaSuccess)
        return;

    auto bound_z = BindHelper::Bind(&tex_z, vel_z, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_z.error() != cudaSuccess)
        return;

    dim3 block(8, 8, 8);
    dim3 grid(volume_size.x / block.x, volume_size.y / block.y,
              volume_size.z / block.z);
    ComputeDivergenceStaggeredKernel<<<grid, block>>>(inverse_cell_size,
                                                      volume_size);
}

void LaunchComputeResidualDiagnosis(cudaArray* residual, cudaArray* u,
                                    cudaArray* b, float inverse_h_square,
                                    uint3 volume_size)
{
    if (BindCudaSurfaceToArray(&surf, residual) != cudaSuccess)
        return;

    auto bound_u = BindHelper::Bind(&tex, u, false, cudaFilterModePoint,
                                    cudaAddressModeClamp);
    if (bound_u.error() != cudaSuccess)
        return;

    auto bound_b = BindHelper::Bind(&tex_b, b, false, cudaFilterModePoint,
                                    cudaAddressModeClamp);
    if (bound_b.error() != cudaSuccess)
        return;

    dim3 block(8, 8, volume_size.x / 8);
    dim3 grid(volume_size.x / block.x, volume_size.y / block.y,
              volume_size.z / block.z);
    ComputeResidualDiagnosisKernel<<<grid, block>>>(inverse_h_square,
                                                    volume_size);
}

void LaunchRoundPassed(int* dest_array, int round, int x)
{
    RoundPassedKernel<<<1, 1>>>(dest_array, round, x);
}

void LaunchSubtractGradient(cudaArray* velocity, cudaArray* pressure,
                            float half_inverse_cell_size, uint3 volume_size,
                            BlockArrangement* ba)
{
    if (BindCudaSurfaceToArray(&surf, velocity) != cudaSuccess)
        return;

    auto bound_vel = BindHelper::Bind(&gradient_velocity, velocity,
                                      false, cudaFilterModePoint,
                                      cudaAddressModeClamp);
    if (bound_vel.error() != cudaSuccess)
        return;

    auto bound = BindHelper::Bind(&tex, pressure, false,
                                  cudaFilterModePoint, cudaAddressModeClamp);
    if (bound.error() != cudaSuccess)
        return;

    dim3 block;
    dim3 grid;
    ba->ArrangePrefer3dLocality(&block, &grid, volume_size);
    SubtractGradientKernel<<<grid, block>>>(half_inverse_cell_size,
                                            volume_size);
}

void LaunchSubtractGradientStaggered(cudaArray* vel_x, cudaArray* vel_y,
                                     cudaArray* vel_z, cudaArray* pressure,
                                     float inverse_cell_size, uint3 volume_size,
                                     BlockArrangement* ba)
{
    if (BindCudaSurfaceToArray(&surf_x, vel_x) != cudaSuccess)
        return;

    if (BindCudaSurfaceToArray(&surf_y, vel_y) != cudaSuccess)
        return;

    if (BindCudaSurfaceToArray(&surf_z, vel_z) != cudaSuccess)
        return;

    auto bound_x = BindHelper::Bind(&tex_x, vel_x, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_x.error() != cudaSuccess)
        return;

    auto bound_y = BindHelper::Bind(&tex_y, vel_y, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_y.error() != cudaSuccess)
        return;

    auto bound_z = BindHelper::Bind(&tex_z, vel_z, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_z.error() != cudaSuccess)
        return;

    auto bound = BindHelper::Bind(&tex, pressure, false, cudaFilterModeLinear,
                                  cudaAddressModeClamp);
    if (bound.error() != cudaSuccess)
        return;

    dim3 block;
    dim3 grid;
    ba->ArrangePrefer3dLocality(&block, &grid, volume_size);
    SubtractGradientStaggeredKernel<<<grid, block>>>(inverse_cell_size,
                                                     volume_size);
}

// =============================================================================

__global__ void ApplyBuoyancyStaggeredKernel2(float time_step,
                                              float ambient_temperature,
                                              float accel_factor, float gravity,
                                              float3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    float3 coord = make_float3(x, 0, z) + 0.5f;
    float t_prev = tex3D(tex_t, coord.x, coord.y, coord.z);
    float d_prev = tex3D(tex_d, coord.x, coord.y, coord.z);
    float accel_prev = time_step *
        ((t_prev - ambient_temperature) * accel_factor - d_prev * gravity);

    float y = 1.5f;
    for (int i = 1; i < volume_size.y; i++, y += 1.0f) {
        float t = tex3D(tex_t, coord.x, y, coord.z);
        float d = tex3D(tex_d, coord.x, y, coord.z);
        float accel = time_step *
            ((t - ambient_temperature) * accel_factor - d * gravity);

        float velocity = tex3D(tex, coord.x, y, coord.z);

        auto r = __float2half_rn(velocity + (accel_prev + accel) * 0.5f);
        surf3Dwrite(r, surf, x * sizeof(r), i, z, cudaBoundaryModeTrap);

        t_prev = t;
        d_prev = d;
        accel_prev = accel;
    }
}

void LaunchApplyBuoyancyStaggered(cudaArray* vel_x, cudaArray* vel_y,
                                  cudaArray* vel_z, cudaArray* temperature,
                                  cudaArray* density, float time_step,
                                  float ambient_temperature, float accel_factor,
                                  float gravity, uint3 volume_size)
{
    if (BindCudaSurfaceToArray(&surf, vel_y) != cudaSuccess)
        return;

    auto bound_v = BindHelper::Bind(&tex, vel_y, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_v.error() != cudaSuccess)
        return;

    auto bound_t = BindHelper::Bind(&tex_t, temperature, false,
                                    cudaFilterModeLinear, cudaAddressModeClamp);
    if (bound_t.error() != cudaSuccess)
        return;

    auto bound_d = BindHelper::Bind(&tex_d, density, false,
                                    cudaFilterModeLinear, cudaAddressModeClamp);
    if (bound_d.error() != cudaSuccess)
        return;

    dim3 block(8, 1, 8);
    dim3 grid(volume_size.x / block.x, 1, volume_size.z / block.z);
    ApplyBuoyancyStaggeredKernel2<<<grid, block>>>(time_step,
                                                   ambient_temperature,
                                                   accel_factor, gravity,
                                                   make_float3(volume_size));
}
