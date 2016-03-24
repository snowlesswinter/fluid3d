#ifndef _GL_TEXTURE_H_
#define _GL_TEXTURE_H_

#include <string>

#include "opengl/glew.h"

struct SurfacePod;
class GLTexture
{
public:
    // Temporary
    static GLTexture FromSurfacePod(const SurfacePod* sp);

    GLTexture();
    ~GLTexture();

    void Bind() const;
    void BindFrameBuffer() const;
    bool Create(int width, int height, int depth, GLint internal_format,
                GLenum format);
    void GetTexImage(GLenum format, GLenum type, void* buffer);
    void Unbind() const;

    GLuint frame_buffer() const { return frame_buffer_; }
    GLuint buffer() const { return buffer_; }
    GLuint handle() const { return handle_; }
    GLenum target() const { return target_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int depth() const { return depth_; }

private:
    GLuint frame_buffer_;
    GLuint buffer_;
    GLuint handle_;
    GLenum target_;
    int width_;
    int height_;
    int depth_;
};

#endif // _GL_PROGRAM_H_