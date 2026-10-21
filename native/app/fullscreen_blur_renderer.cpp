#include "fullscreen_blur_renderer.hpp"
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define BLUR_SHADER "#version 300 es\nprecision highp float;\n"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#define BLUR_SHADER "#version 330 core\n"
#endif
#include <array>
#include <stdexcept>
#include <string>
namespace hades {
namespace {
void check(const char*stage){const auto e=glGetError();if(e!=GL_NO_ERROR)throw std::runtime_error(std::string("Native fullscreen blur GL ")+stage+" error="+std::to_string(e));}
unsigned compile(GLenum type,const char*text){const auto s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);int ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);throw std::runtime_error(std::string("Native fullscreen blur shader: ")+log);}return s;}
// Full-target triangle. v_TexCoord uses the D3D top-left origin of the
// original Default.vert/Shroud.vert texture coordinates.
const char* vertexSource=BLUR_SHADER R"(
out vec2 v_TexCoord;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));v_TexCoord=vec2(p.x,1.-p.y);gl_Position=vec4(p*2.-1.,0.,1.);})";
unsigned program(const char*fragment){const auto v=compile(GL_VERTEX_SHADER,vertexSource);unsigned f=0;try{f=compile(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(v);throw;}const auto p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);int ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("Native fullscreen blur link: ")+log);}return p;}
struct State {
 GLint program=0,vao=0,active=0,read=0,draw=0,texture=0,sampler=0,src=0,dst=0,srcA=0,dstA=0;std::array<GLint,4>viewport{};std::array<GLboolean,4>mask{};bool blend,depth,scissor,cull;
 State():blend(glIsEnabled(GL_BLEND)),depth(glIsEnabled(GL_DEPTH_TEST)),scissor(glIsEnabled(GL_SCISSOR_TEST)),cull(glIsEnabled(GL_CULL_FACE)){glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_VIEWPORT,viewport.data());glGetBooleanv(GL_COLOR_WRITEMASK,mask.data());glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcA);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstA);}
 ~State(){glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,GLuint(texture));glBindSampler(0,GLuint(sampler));glActiveTexture(GLenum(active));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);glBlendFuncSeparate(GLenum(src),GLenum(dst),GLenum(srcA),GLenum(dstA));for(auto p:{std::pair<GLenum,bool>{GL_BLEND,blend},{GL_DEPTH_TEST,depth},{GL_SCISSOR_TEST,scissor},{GL_CULL_FACE,cull}})if(p.second)glEnable(p.first);else glDisable(p.first);}
};
}
void FullscreenBlurRenderer::initialize(){if(vao_)return;
 // RadialBlur.frag main(): eleven taps along the centre direction, averaged,
 // then lerp(color,sum,clamp(dist*sampleStrength,0,1)). Texture = scene copy.
 const char*blur=BLUR_SHADER R"(
in vec2 v_TexCoord;uniform sampler2D Texture;uniform float sampleDist;uniform float sampleStrength;out vec4 outColor;
vec4 tap(vec2 uv){return texture(Texture,vec2(uv.x,1.-uv.y));}
void main(){vec2 dir=.5-v_TexCoord;float dist=sqrt(dir.x*dir.x+dir.y*dir.y);dir=dir/dist;dir=dir*sampleDist;
vec4 color=tap(v_TexCoord);vec4 sum=color;
sum+=tap(v_TexCoord+-0.08*dir);sum+=tap(v_TexCoord+-0.05*dir);sum+=tap(v_TexCoord+-0.03*dir);sum+=tap(v_TexCoord+-0.02*dir);sum+=tap(v_TexCoord+-0.01*dir);
sum+=tap(v_TexCoord+0.01*dir);sum+=tap(v_TexCoord+0.02*dir);sum+=tap(v_TexCoord+0.03*dir);sum+=tap(v_TexCoord+0.05*dir);sum+=tap(v_TexCoord+0.08*dir);
sum*=1./11.;float t=clamp(dist*sampleStrength,0.,1.);outColor=mix(color,sum,t);})";
 // Shroud.frag main() with Shroud.vert v_TexCoord12.zw = uv-0.5 and
 // SampleFromFileTinted (PREMUL). The rectangle samples a blank white texel.
 const char*shroud=BLUR_SHADER R"(
in vec2 v_TexCoord;uniform sampler2D Texture;uniform float Radius;uniform vec4 tintColor;out vec4 outColor;
float rand(vec2 seed){return fract(sin(dot(seed*137.23890,vec2(12.9898,78.233)))*43758.5453);}
void main(){vec2 zw=v_TexCoord-vec2(.5);float normdist=dot(zw,zw)*4.;float randFrac=(rand(v_TexCoord)-.5)*(1.+Radius)/65.;float modifier=(normdist-Radius)+randFrac;
vec4 tint=tintColor;tint.a*=step(Radius,normdist)*modifier;if(tint.a<=0.)discard;vec4 c=texture(Texture,v_TexCoord)*tint;c.rgb*=c.a;outColor=c;})";
 try{blurProgram_=program(blur);shroudProgram_=program(shroud);glGenVertexArrays(1,&vao_);glGenTextures(1,&blankTexture_);glGenTextures(1,&copy_);
  const std::array<unsigned char,4> white{255,255,255,255};glBindTexture(GL_TEXTURE_2D,blankTexture_);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white.data());
  for(auto t:{blankTexture_,copy_}){glBindTexture(GL_TEXTURE_2D,t);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
 }catch(...){shutdown();throw;}
}
void FullscreenBlurRenderer::renderRadialBlur(const RadialBlurState& b,int w,int h){
 if(!b.shouldDraw())return;if(w<1||h<1||w>16384||h>16384)throw std::runtime_error("Native radial blur viewport bounds");
 State saved;check("state query");initialize();
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,copy_);if(w!=width_||h!=height_){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);width_=w;height_=h;}
 glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(saved.draw));glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,w,h);check("framebuffer copy");
 glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,w,h);glBindVertexArray(vao_);glBindSampler(0,0);
 glUseProgram(blurProgram_);glUniform1i(glGetUniformLocation(blurProgram_,"Texture"),0);glUniform1f(glGetUniformLocation(blurProgram_,"sampleDist"),b.sampleDist());glUniform1f(glGetUniformLocation(blurProgram_,"sampleStrength"),b.sampleStrength());
 glDrawArrays(GL_TRIANGLES,0,3);check("radial blur");
}
void FullscreenBlurRenderer::renderVignetteFrame(const VignetteFrameState::Draw& d,int w,int h){
 if(!d.visible)return;if(w<1||h<1||w>16384||h>16384)throw std::runtime_error("Native vignette viewport bounds");
 State saved;check("state query");initialize();
 // APPROXIMATION: premultiplied-alpha source-over blend for the Shroud draw
 // (the shader premultiplies); DrawManager blend state selection not traced.
 glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glViewport(0,0,w,h);glBindVertexArray(vao_);glBindSampler(0,0);
 glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,blankTexture_);glUseProgram(shroudProgram_);glUniform1i(glGetUniformLocation(shroudProgram_,"Texture"),0);glUniform1f(glGetUniformLocation(shroudProgram_,"Radius"),d.radius);
 glUniform4f(glGetUniformLocation(shroudProgram_,"tintColor"),d.rgba[0]/255.f,d.rgba[1]/255.f,d.rgba[2]/255.f,d.rgba[3]/255.f);
 glDrawArrays(GL_TRIANGLES,0,3);check("vignette frame");
}
void FullscreenBlurRenderer::shutdown(){if(blankTexture_)glDeleteTextures(1,&blankTexture_);if(copy_)glDeleteTextures(1,&copy_);if(vao_)glDeleteVertexArrays(1,&vao_);if(blurProgram_)glDeleteProgram(blurProgram_);if(shroudProgram_)glDeleteProgram(shroudProgram_);blankTexture_=copy_=vao_=blurProgram_=shroudProgram_=0;width_=height_=0;}
}
