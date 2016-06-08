#include <cassert>

#include "third_party/opengl/glew.h"

#include <helper_math.h>

#include "advection_method.h"
#include "block_arrangement.h"
#include "field_offset.h"
#include "cuda_common.h"

surface<void, cudaSurfaceType3D> surf;
surface<void, cudaSurfaceType3D> surf_x;
surface<void, cudaSurfaceType3D> surf_y;
surface<void, cudaSurfaceType3D> surf_z;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_x;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_y;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_z;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_aux;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_vx;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_vy;
texture<ushort, cudaTextureType3D, cudaReadModeNormalizedFloat> tex_vz;

__device__ float3 GetVelocityStaggeredOffset(float3 pos)
{
    float v_x = tex3D(tex_vx, pos.x + 0.5f, pos.y,        pos.z       );
    float v_y = tex3D(tex_vy, pos.x,        pos.y + 0.5f, pos.z       );
    float v_z = tex3D(tex_vz, pos.x,        pos.y,        pos.z + 0.5f);
    return make_float3(v_x, v_y, v_z);
}

// =============================================================================

template <bool MidPoint>
__device__ inline float3 AdvectImpl(float3 vel, float3 pos, float time_step)
{
    return pos - vel * time_step;
}

template <>
__device__ inline float3 AdvectImpl<true>(float3 vel, float3 pos, float time_step)
{
    float3 mid_point = pos - vel * 0.5f * time_step;
    vel = GetVelocityStaggeredOffset(mid_point);
    return pos - vel * time_step;
}

template <bool MidPoint>
__global__ void AdvectFieldBfeccStaggeredOffsetKernel(float3 offset,
                                                      float time_step,
                                                      float dissipation,
                                                      uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    if (x >= volume_size.x || y >= volume_size.y || z >= volume_size.z)
        return;

    float3 coord = make_float3(x, y, z) + 0.5f;
    float3 vel = GetVelocityStaggeredOffset(coord + offset);
    float3 back_traced = AdvectImpl<MidPoint>(vel, coord, time_step);

    float ��0 = tex3D(tex, back_traced.x - 0.5f, back_traced.y - 0.5f, back_traced.z - 0.5f);
    float ��1 = tex3D(tex, back_traced.x - 0.5f, back_traced.y - 0.5f, back_traced.z + 0.5f);
    float ��2 = tex3D(tex, back_traced.x - 0.5f, back_traced.y + 0.5f, back_traced.z - 0.5f);
    float ��3 = tex3D(tex, back_traced.x - 0.5f, back_traced.y + 0.5f, back_traced.z + 0.5f);
    float ��4 = tex3D(tex, back_traced.x + 0.5f, back_traced.y - 0.5f, back_traced.z - 0.5f);
    float ��5 = tex3D(tex, back_traced.x + 0.5f, back_traced.y - 0.5f, back_traced.z + 0.5f);
    float ��6 = tex3D(tex, back_traced.x + 0.5f, back_traced.y + 0.5f, back_traced.z - 0.5f);
    float ��7 = tex3D(tex, back_traced.x + 0.5f, back_traced.y + 0.5f, back_traced.z + 0.5f);

    float ��_min = fminf(fminf(fminf(fminf(fminf(fminf(fminf(��0, ��1), ��2), ��3), ��4), ��5), ��6), ��7);
    float ��_max = fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(��0, ��1), ��2), ��3), ��4), ��5), ��6), ��7);

    float ��_new = tex3D(tex_aux, back_traced.x, back_traced.y, back_traced.z);
    float clamped = fmaxf(fminf(��_new, ��_max), ��_min);
    if (clamped != ��_new) // New extrema found, revert to the first order
                          // accurate semi-Lagrangian method.
        ��_new = tex3D(tex, back_traced.x, back_traced.y, back_traced.z);

    auto r = __float2half_rn((1.0f - dissipation * time_step) * ��_new);
    surf3Dwrite(r, surf, x * sizeof(r), y, z, cudaBoundaryModeTrap);
}

template <bool MidPoint>
__global__ void AdvectFieldMacCormackStaggeredOffsetKernel(float3 offset,
                                                           float time_step,
                                                           float dissipation,
                                                           uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    if (x >= volume_size.x || y >= volume_size.y || z >= volume_size.z)
        return;

    float3 coord = make_float3(x, y, z) + 0.5f;

    float3 vel = GetVelocityStaggeredOffset(coord + offset);
    float3 back_traced = AdvectImpl<MidPoint>(vel, coord, time_step);

    float ��_n = tex3D(tex, coord.x, coord.y, coord.z);

    float ��0 = tex3D(tex, back_traced.x - 0.5f, back_traced.y - 0.5f, back_traced.z - 0.5f);
    float ��1 = tex3D(tex, back_traced.x - 0.5f, back_traced.y - 0.5f, back_traced.z + 0.5f);
    float ��2 = tex3D(tex, back_traced.x - 0.5f, back_traced.y + 0.5f, back_traced.z - 0.5f);
    float ��3 = tex3D(tex, back_traced.x - 0.5f, back_traced.y + 0.5f, back_traced.z + 0.5f);
    float ��4 = tex3D(tex, back_traced.x + 0.5f, back_traced.y - 0.5f, back_traced.z - 0.5f);
    float ��5 = tex3D(tex, back_traced.x + 0.5f, back_traced.y - 0.5f, back_traced.z + 0.5f);
    float ��6 = tex3D(tex, back_traced.x + 0.5f, back_traced.y + 0.5f, back_traced.z - 0.5f);
    float ��7 = tex3D(tex, back_traced.x + 0.5f, back_traced.y + 0.5f, back_traced.z + 0.5f);

    float ��_min = fminf(fminf(fminf(fminf(fminf(fminf(fminf(��0, ��1), ��2), ��3), ��4), ��5), ��6), ��7);
    float ��_max = fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(fmaxf(��0, ��1), ��2), ��3), ��4), ��5), ��6), ��7);

    float ��_np1_hat = tex3D(tex_aux, coord.x, coord.y, coord.z);

    float3 forward_trace = AdvectImpl<MidPoint>(vel, coord, -time_step);
    float ��_n_hat = tex3D(tex_aux, forward_trace.x, forward_trace.y, forward_trace.z);

    float ��_new = ��_np1_hat + 0.5f * (��_n - ��_n_hat);
    float clamped = fmaxf(fminf(��_new, ��_max), ��_min);
    if (clamped != ��_new)
        ��_new = ��_np1_hat;

    ��_new *= (1.0f - dissipation * time_step);
    auto r = __float2half_rn(��_new);
    surf3Dwrite(r, surf, x * sizeof(r), y, z, cudaBoundaryModeTrap);
}

template <bool MidPoint>
__global__ void AdvectFieldSemiLagrangianStaggeredOffsetKernel(
    float3 offset, float time_step, float dissipation, uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    if (x >= volume_size.x || y >= volume_size.y || z >= volume_size.z)
        return;

    float3 coord = make_float3(x, y, z) + 0.5f;

    float3 vel = GetVelocityStaggeredOffset(coord + offset);
    float3 back_traced = AdvectImpl<MidPoint>(vel, coord, time_step);
    float �� = tex3D(tex, back_traced.x, back_traced.y, back_traced.z);
    �� *= (1.0f - dissipation * time_step);
    auto r = __float2half_rn(��);
    surf3Dwrite(r, surf, x * sizeof(r), y, z, cudaBoundaryModeTrap);
}

template <bool MidPoint>
__global__ void BfeccRemoveErrorStaggeredOffsetKernel(float3 offset,
                                                      float time_step,
                                                      uint3 volume_size)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int z = blockIdx.z * blockDim.z + threadIdx.z;

    if (x >= volume_size.x || y >= volume_size.y || z >= volume_size.z)
        return;

    float3 coord = make_float3(x, y, z) + 0.5f;
    float3 vel = GetVelocityStaggeredOffset(coord + offset);
    float3 forward_trace = AdvectImpl<MidPoint>(vel, coord, -time_step);

    float �� = tex3D(tex, coord.x, coord.y, coord.z);
    float r = tex3D(tex_aux, forward_trace.x, forward_trace.y, forward_trace.z);
    r = 0.5f * (3.0f * �� - r);
    surf3Dwrite(__float2half_rn(r), surf, x * sizeof(ushort), y, z,
                cudaBoundaryModeTrap);
}

void AdvectFieldsBfeccStaggeredOffset(cudaArray** fnp1, cudaArray** fn,
                                      float3* offset, int num_of_fields,
                                      cudaArray* vel_x, cudaArray* vel_y,
                                      cudaArray* vel_z, cudaArray* aux,
                                      float time_step, float dissipation,
                                      uint3 volume_size, bool mid_point,
                                      BlockArrangement* ba)
{
    auto bound_vx = BindHelper::Bind(&tex_vx, vel_x, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vx.error() != cudaSuccess)
        return;

    auto bound_vy = BindHelper::Bind(&tex_vy, vel_y, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vy.error() != cudaSuccess)
        return;

    auto bound_vz = BindHelper::Bind(&tex_vz, vel_z, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vz.error() != cudaSuccess)
        return;

    dim3 block;
    dim3 grid;
    ba->ArrangePrefer3dLocality(&block, &grid, volume_size);
    for (int i = 0; i < num_of_fields; i++) {
        // Pass 1: Calculate ��_n_plus_1_hat, and store in |fnp1[i]|.
        if (BindCudaSurfaceToArray(&surf, fnp1[i]) != cudaSuccess)
            return;

        auto bound = BindHelper::Bind(&tex, fn[i], false, cudaFilterModeLinear,
                                      cudaAddressModeClamp);
        if (bound.error() != cudaSuccess)
            return;

        if (mid_point)
            AdvectFieldSemiLagrangianStaggeredOffsetKernel<true><<<grid, block>>>(
                offset[i], time_step, 0.0f, volume_size);
        else
            AdvectFieldSemiLagrangianStaggeredOffsetKernel<false><<<grid, block>>>(
                offset[i], time_step, 0.0f, volume_size);

        // Pass 2: Calculate ��_n_hat, and store in |aux|.
        if (BindCudaSurfaceToArray(&surf, aux) != cudaSuccess)
            return;

        {
            auto bound_a = BindHelper::Bind(&tex_aux, fnp1[i], false,
                                            cudaFilterModeLinear,
                                            cudaAddressModeClamp);
            if (bound_a.error() != cudaSuccess)
                return;

            if (mid_point)
                BfeccRemoveErrorStaggeredOffsetKernel<true><<<grid, block>>>(
                    offset[i], time_step, volume_size);
            else
                BfeccRemoveErrorStaggeredOffsetKernel<false><<<grid, block>>>(
                    offset[i], time_step, volume_size);
        }

        // Pass 3: Calculate the final result.
        if (BindCudaSurfaceToArray(&surf, fnp1[i]) != cudaSuccess)
            return;

        auto bound_a = BindHelper::Bind(&tex_aux, aux, false,
                                        cudaFilterModeLinear,
                                        cudaAddressModeClamp);
        if (bound_a.error() != cudaSuccess)
            return;

        if (mid_point)
            AdvectFieldBfeccStaggeredOffsetKernel<true><<<grid, block>>>(
                offset[i], time_step, dissipation, volume_size);
        else
            AdvectFieldBfeccStaggeredOffsetKernel<false><<<grid, block>>>(
                offset[i], time_step, dissipation, volume_size);
    }
}

void AdvectFieldsMacCormackStaggeredOffset(cudaArray** fnp1, cudaArray** fn,
                                           float3* offset, int num_of_fields,
                                           cudaArray* vel_x, cudaArray* vel_y,
                                           cudaArray* vel_z, cudaArray* aux,
                                           float time_step, float dissipation,
                                           uint3 volume_size, bool mid_point,
                                           BlockArrangement* ba)
{
    auto bound_vx = BindHelper::Bind(&tex_vx, vel_x, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vx.error() != cudaSuccess)
        return;

    auto bound_vy = BindHelper::Bind(&tex_vy, vel_y, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vy.error() != cudaSuccess)
        return;

    auto bound_vz = BindHelper::Bind(&tex_vz, vel_z, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vz.error() != cudaSuccess)
        return;

    auto bound_a = BindHelper::Bind(&tex_aux, aux, false, cudaFilterModeLinear,
                                    cudaAddressModeClamp);
    if (bound_a.error() != cudaSuccess)
        return;

    dim3 block;
    dim3 grid;
    ba->ArrangePrefer3dLocality(&block, &grid, volume_size);
    for (int i = 0; i < num_of_fields; i++) {
        if (BindCudaSurfaceToArray(&surf, aux) != cudaSuccess)
            return;

        auto bound = BindHelper::Bind(&tex, fn[i], false, cudaFilterModeLinear,
                                      cudaAddressModeClamp);
        if (bound.error() != cudaSuccess)
            return;

        if (mid_point)
            AdvectFieldSemiLagrangianStaggeredOffsetKernel<true><<<grid, block>>>(
                offset[i], time_step, 0.0f, volume_size);
        else
            AdvectFieldSemiLagrangianStaggeredOffsetKernel<false><<<grid, block>>>(
                offset[i], time_step, 0.0f, volume_size);

        if (BindCudaSurfaceToArray(&surf, fnp1[i]) != cudaSuccess)
            return;

        if (mid_point)
            AdvectFieldMacCormackStaggeredOffsetKernel<true><<<grid, block>>>(
                offset[i], time_step, dissipation, volume_size);
        else
            AdvectFieldMacCormackStaggeredOffsetKernel<false><<<grid, block>>>(
                offset[i], time_step, dissipation, volume_size);
    }
}

void AdvectFieldsSemiLagrangianStaggeredOffset(cudaArray** fnp1, cudaArray** fn,
                                               float3* offset,
                                               int num_of_fields,
                                               cudaArray* vel_x,
                                               cudaArray* vel_y,
                                               cudaArray* vel_z,
                                               float time_step,
                                               float dissipation,
                                               uint3 volume_size,
                                               bool mid_point,
                                               BlockArrangement* ba)
{
    auto bound_vx = BindHelper::Bind(&tex_vx, vel_x, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vx.error() != cudaSuccess)
        return;

    auto bound_vy = BindHelper::Bind(&tex_vy, vel_y, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vy.error() != cudaSuccess)
        return;

    auto bound_vz = BindHelper::Bind(&tex_vz, vel_z, false,
                                     cudaFilterModeLinear,
                                     cudaAddressModeClamp);
    if (bound_vz.error() != cudaSuccess)
        return;

    dim3 block;
    dim3 grid;
    ba->ArrangePrefer3dLocality(&block, &grid, volume_size);
    for (int i = 0; i < num_of_fields; i++) {
        if (BindCudaSurfaceToArray(&surf, fnp1[i]) != cudaSuccess)
            return;

        auto bound = BindHelper::Bind(&tex, fn[i], false, cudaFilterModeLinear,
                                      cudaAddressModeClamp);
        if (bound.error() != cudaSuccess)
            return;

        if (mid_point)
            AdvectFieldSemiLagrangianStaggeredOffsetKernel<true><<<grid, block>>>(
                offset[i], time_step, 0.0f, volume_size);
        else
            AdvectFieldSemiLagrangianStaggeredOffsetKernel<false><<<grid, block>>>(
                offset[i], time_step, 0.0f, volume_size);
    }
}

void LaunchAdvectScalarFieldStaggered(cudaArray* fnp1, cudaArray* fn,
                                      cudaArray* vel_x, cudaArray* vel_y,
                                      cudaArray* vel_z, cudaArray* aux,
                                      float time_step, float dissipation,
                                      AdvectionMethod method,
                                      uint3 volume_size, bool mid_point,
                                      BlockArrangement* ba)
{
    cudaArray* fnp1s[] = {fnp1};
    cudaArray* fns[] = {fn};
    float3 offsets[] = {make_float3(0.0f)};
    int num_of_fields = sizeof(fnp1s) / sizeof(fnp1s[0]);
    if (method == MACCORMACK_SEMI_LAGRANGIAN) {
        AdvectFieldsMacCormackStaggeredOffset(fnp1s, fns, offsets,
                                              num_of_fields, vel_x, vel_y,
                                              vel_z, aux, time_step,
                                              dissipation, volume_size,
                                              mid_point, ba);
    } else if (method == BFECC_SEMI_LAGRANGIAN) {
        AdvectFieldsBfeccStaggeredOffset(fnp1s, fns, offsets, num_of_fields,
                                         vel_x, vel_y, vel_z, aux, time_step,
                                         dissipation, volume_size, mid_point,
                                         ba);
    } else {
        AdvectFieldsSemiLagrangianStaggeredOffset(fnp1s, fns, offsets,
                                                  num_of_fields, vel_x, vel_y,
                                                  vel_z, time_step, dissipation,
                                                  volume_size, mid_point, ba);
    }
}

void LaunchAdvectVelocityStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y,
                                   cudaArray* fnp1_z, cudaArray* fn_x,
                                   cudaArray* fn_y, cudaArray* fn_z,
                                   cudaArray* vel_x, cudaArray* vel_y,
                                   cudaArray* vel_z, cudaArray* aux,
                                   float time_step, float dissipation,
                                   AdvectionMethod method,
                                   uint3 volume_size, bool mid_point,
                                   BlockArrangement* ba)
{
    cudaArray* fnp1s[] = {fnp1_x, fnp1_y, fnp1_z};
    cudaArray* fns[] = {fn_x, fn_y, fn_z};
    float3 offsets[] = {
        -GetOffsetVelocityField(0),
        -GetOffsetVelocityField(1),
        -GetOffsetVelocityField(2)
    };
    int num_of_fields = sizeof(fnp1s) / sizeof(fnp1s[0]);
    if (method == MACCORMACK_SEMI_LAGRANGIAN) {
        AdvectFieldsMacCormackStaggeredOffset(fnp1s, fns, offsets,
                                              num_of_fields, vel_x, vel_y,
                                              vel_z, aux, time_step,
                                              dissipation, volume_size,
                                              mid_point, ba);
    } else if (method == BFECC_SEMI_LAGRANGIAN) {
        AdvectFieldsBfeccStaggeredOffset(fnp1s, fns, offsets, num_of_fields,
                                         vel_x, vel_y, vel_z, aux, time_step,
                                         dissipation, volume_size, mid_point,
                                         ba);
    } else {
        AdvectFieldsSemiLagrangianStaggeredOffset(fnp1s, fns, offsets,
                                                  num_of_fields, vel_x, vel_y,
                                                  vel_z, time_step, dissipation,
                                                  volume_size, mid_point, ba);
    }
}

void LaunchAdvectVorticityStaggered(cudaArray* fnp1_x, cudaArray* fnp1_y,
                                    cudaArray* fnp1_z, cudaArray* fn_x,
                                    cudaArray* fn_y, cudaArray* fn_z,
                                    cudaArray* vel_x, cudaArray* vel_y,
                                    cudaArray* vel_z, cudaArray* aux,
                                    float time_step, float dissipation,
                                    AdvectionMethod method,
                                    uint3 volume_size, bool mid_point,
                                    BlockArrangement* ba)
{
    cudaArray* fnp1s[] = {fnp1_x, fnp1_y, fnp1_z};
    cudaArray* fns[] = {fn_x, fn_y, fn_z};
    float3 offsets[] = {
        -GetOffsetVorticityField(0),
        -GetOffsetVorticityField(1),
        -GetOffsetVorticityField(2)
    };
    int num_of_fields = sizeof(fnp1s) / sizeof(fnp1s[0]);
    if (method == MACCORMACK_SEMI_LAGRANGIAN) {
        AdvectFieldsMacCormackStaggeredOffset(fnp1s, fns, offsets,
                                              num_of_fields, vel_x, vel_y,
                                              vel_z, aux, time_step,
                                              dissipation, volume_size,
                                              mid_point, ba);
    } else if (method == BFECC_SEMI_LAGRANGIAN) {
        AdvectFieldsBfeccStaggeredOffset(fnp1s, fns, offsets, num_of_fields,
                                         vel_x, vel_y, vel_z, aux, time_step,
                                         dissipation, volume_size, mid_point,
                                         ba);
    } else {
        AdvectFieldsSemiLagrangianStaggeredOffset(fnp1s, fns, offsets,
                                                  num_of_fields, vel_x, vel_y,
                                                  vel_z, time_step, dissipation,
                                                  volume_size, mid_point, ba);
    }
}
