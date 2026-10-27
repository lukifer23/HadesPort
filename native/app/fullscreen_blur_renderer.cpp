#include "fullscreen_blur_renderer.hpp"
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define RADIAL_SHADER "#version 300 es\nprecision highp float;\n"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#define RADIAL_SHADER "#version 330 core\n"
#endif
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <string>
namespace hades {
namespace {
void check(const char*stage){const auto e=glGetError();if(e!=GL_NO_ERROR)throw std::runtime_error(std::string("Native radial blur GL ")+stage+" error="+std::to_string(e));}
unsigned compile(GLenum type,const char*text){const auto s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);int ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);throw std::runtime_error(std::string("Native radial blur shader: ")+log);}return s;}
unsigned program(const char*fragment){const char*vertex=RADIAL_SHADER R"(
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(p*2.-1.,0.,1.);})";
 const auto v=compile(GL_VERTEX_SHADER,vertex);unsigned f=0;try{f=compile(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(v);throw;}const auto p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);int ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("Native radial blur link: ")+log);}return p;
}
struct State {
 GLint program=0,vao=0,active=0,read=0,draw=0,texture=0,sampler=0;std::array<GLint,4>viewport{};std::array<GLboolean,4>mask{};bool blend,depth,scissor,cull;
 State():blend(glIsEnabled(GL_BLEND)),depth(glIsEnabled(GL_DEPTH_TEST)),scissor(glIsEnabled(GL_SCISSOR_TEST)),cull(glIsEnabled(GL_CULL_FACE)){glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_VIEWPORT,viewport.data());glGetBooleanv(GL_COLOR_WRITEMASK,mask.data());glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);}
 ~State(){glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,GLuint(texture));glBindSampler(0,GLuint(sampler));glActiveTexture(GLenum(active));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);for(auto p:{std::pair<GLenum,bool>{GL_BLEND,blend},{GL_DEPTH_TEST,depth},{GL_SCISSOR_TEST,scissor},{GL_CULL_FACE,cull}})if(p.second)glEnable(p.first);else glDisable(p.first);}
};
}
void FullscreenBlurRenderer::initialize(){if(vao_)return;
 // RadialBlur.frag main(), statement for statement. Portable adaptation: the
 // full-viewport v_TexCoord is gl_FragCoord/imageSize on the GL copy. The
 // original D3D top-left UV origin flips both the fragment UV and every tap
 // about y=.5 identically (dir is relative to the centre), so no flip is needed.
 // HLSL lerp(a,b,t) is a+t*(b-a).
 const char*fragment=RADIAL_SHADER R"(
uniform sampler2D Texture;uniform vec2 imageSize;uniform float sampleDist;uniform float sampleStrength;out vec4 finalColor;
void main(){vec2 uv=gl_FragCoord.xy/imageSize;vec2 dir=.5-uv;float dist=sqrt(dir.x*dir.x+dir.y*dir.y);dir=dir/dist;dir=dir*sampleDist;
vec4 color=texture(Texture,uv);vec4 sum=color;
sum+=texture(Texture,uv+-0.08*dir);sum+=texture(Texture,uv+-0.05*dir);sum+=texture(Texture,uv+-0.03*dir);sum+=texture(Texture,uv+-0.02*dir);sum+=texture(Texture,uv+-0.01*dir);
sum+=texture(Texture,uv+0.01*dir);sum+=texture(Texture,uv+0.02*dir);sum+=texture(Texture,uv+0.03*dir);sum+=texture(Texture,uv+0.05*dir);sum+=texture(Texture,uv+0.08*dir);
sum*=1.0/11.0;float t=dist*sampleStrength;t=clamp(t,0.0,1.0);finalColor=color+t*(sum-color);})";
 try{program_=program(fragment);glGenVertexArrays(1,&vao_);glGenTextures(1,&copy_);
  // APPROXIMATION: register s0 sampler state for the RadialBlur Begin is not
  // traced; linear/clamp matches this port's other fullscreen copies.
  glBindTexture(GL_TEXTURE_2D,copy_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
 }catch(...){shutdown();throw;}
}
void FullscreenBlurRenderer::renderRadialBlur(const RadialBlurState&b,int w,int h){
 if(!b.shouldDraw())return;if(w<1||h<1||w>16384||h>16384)throw std::runtime_error("Native radial blur viewport bounds");
 if(!std::isfinite(b.sampleDist())||!std::isfinite(b.sampleStrength()))throw std::runtime_error("Native radial blur scalars nonfinite");
 State saved;check("state query");initialize();check("initialization");
 // BlurComponent::Draw renders source -> its own target (blur+0xb0), then
 // DrawFullscreenQuadWithScissor copies into the output: same pixels as
 // drawing the pass from a copy of the current target.
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,copy_);if(w!=width_||h!=height_){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);width_=w;height_=h;}
 glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(saved.draw));glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,w,h);check("framebuffer copy");
 glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,w,h);glBindVertexArray(vao_);glBindSampler(0,0);
 glUseProgram(program_);glUniform1i(glGetUniformLocation(program_,"Texture"),0);glUniform2f(glGetUniformLocation(program_,"imageSize"),float(w),float(h));
 glUniform1f(glGetUniformLocation(program_,"sampleDist"),b.sampleDist());glUniform1f(glGetUniformLocation(program_,"sampleStrength"),b.sampleStrength());
 glDrawArrays(GL_TRIANGLES,0,3);check("draw");
}
void FullscreenBlurRenderer::renderVignetteFrame(const VignetteFrameDraw&d,int,int){
 if(!d.visible)return;
 // Recovered: Shroud effect 0x2f (GameplayScreen::LoadContent 0x1801afef4),
 // Radius param, DrawRectangle 0x180227c10 over the camera viewport with the
 // packed color and AssetManager::GetBlankTexture (Dev\blank, opaque white).
 // Not proven: the atlas texcoords Shroud.vert receives and the Begin blend
 // pipeline (DoBeginForge 0x1802290b0 -> 0x180235519).
 throw std::runtime_error("Camera::DrawVignetteFrame visible Shroud frame (Dev\\blank texcoords, Begin blend pipeline) not reconstructed");
}
void FullscreenBlurRenderer::shutdown(){if(copy_)glDeleteTextures(1,&copy_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);copy_=vao_=program_=0;width_=height_=0;}
}
