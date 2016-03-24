#ifndef _CUDA_MAIN_H_
#define _CUDA_MAIN_H_

#include <map>
#include <memory>

namespace Vectormath
{
namespace Aos
{
class Vector3;
}
}
class CudaCore;
class GLTexture;
class GraphicsResource;
class CudaMain
{
public:
    static CudaMain* Instance();

    CudaMain();
    ~CudaMain();

    bool Init();
    int RegisterGLImage(std::shared_ptr<GLTexture> texture);
    void Absolute(std::shared_ptr<GLTexture> texture);
    void ProlongatePacked(std::shared_ptr<GLTexture> coarse,
                          std::shared_ptr<GLTexture> fine);
    void AdvectVelocity(std::shared_ptr<GLTexture> velocity,
                        std::shared_ptr<GLTexture> dest, float time_step,
                        float dissipation);
    void Advect(std::shared_ptr<GLTexture> velocity,
                std::shared_ptr<GLTexture> source,
                std::shared_ptr<GLTexture> dest, float time_step,
                float dissipation);
    void ApplyBuoyancy(std::shared_ptr<GLTexture> velocity,
                       std::shared_ptr<GLTexture> temperature,
                       std::shared_ptr<GLTexture> dest, float time_step,
                       float ambient_temperature, float accel_factor,
                       float gravity);
    void ApplyImpulse(std::shared_ptr<GLTexture> dest,
                      const Vectormath::Aos::Vector3& center_point,
                      const Vectormath::Aos::Vector3& hotspot, float radius,
                      float value);

    // For diagnosis
    void RoundPassed(int round);

    // temporary ===============================================================
    CudaCore* core() { return core_.get(); }

private:
    std::unique_ptr<CudaCore> core_;
    std::map<std::shared_ptr<GLTexture>, std::unique_ptr<GraphicsResource>>
        registerd_textures_;
};

#endif // _CUDA_MAIN_H_