#include "bloom_renderer.hpp"
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define BLOOM_SHADER "#version 300 es\nprecision highp float;\n"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#define BLOOM_SHADER "#version 330 core\n"
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace hades {
BloomKernel makeBloomKernel(float blur){
 if(!std::isfinite(blur)||blur<=0)throw std::runtime_error("Active native bloom requires positive blur");
 BloomKernel out;out.weights[7]=1;float total=1;
 for(int i=1;i<=7;++i){const float w=float(std::exp(-double(i*i)/(2*double(blur)*blur)));out.weights[7-i]=out.weights[7+i]=w;total+=2*w;out.offsets[7-i]=float(2*(i-1))+1.5f;out.offsets[7+i]=-out.offsets[7-i];}
 for(auto&w:out.weights)w/=total;return out;
}
namespace {
void check(const char*stage){const auto e=glGetError();if(e!=GL_NO_ERROR)throw std::runtime_error(std::string("Native bloom GL ")+stage+" error="+std::to_string(e));}
unsigned compile(GLenum type,const char*text){const auto s=glCreateShader(type);glShaderSource(s,1,&text,nullptr);glCompileShader(s);int ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048]{};glGetShaderInfoLog(s,sizeof(log),nullptr,log);glDeleteShader(s);throw std::runtime_error(std::string("Native bloom shader: ")+log);}return s;}
unsigned program(const char*fragment){const char*vertex=BLOOM_SHADER R"(
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(p*2.-1.,0.,1.);})";
 const auto v=compile(GL_VERTEX_SHADER,vertex);unsigned f=0;try{f=compile(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(v);throw;}const auto p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);int ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[2048]{};glGetProgramInfoLog(p,sizeof(log),nullptr,log);glDeleteProgram(p);throw std::runtime_error(std::string("Native bloom link: ")+log);}return p;
}
struct State {
 GLint program=0,vao=0,active=0,read=0,draw=0;std::array<GLint,4>viewport{};std::array<GLint,2>textures{},samplers{};std::array<GLboolean,4>mask{};bool blend,depth,scissor,cull;
 State():blend(glIsEnabled(GL_BLEND)),depth(glIsEnabled(GL_DEPTH_TEST)),scissor(glIsEnabled(GL_SCISSOR_TEST)),cull(glIsEnabled(GL_CULL_FACE)){
 glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_VIEWPORT,viewport.data());glGetBooleanv(GL_COLOR_WRITEMASK,mask.data());for(unsigned i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);glGetIntegerv(GL_SAMPLER_BINDING,&samplers[i]);}}
 ~State(){glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(draw));glUseProgram(GLuint(program));glBindVertexArray(GLuint(vao));for(unsigned i=0;i<2;++i){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,GLuint(textures[i]));glBindSampler(i,GLuint(samplers[i]));}glActiveTexture(GLenum(active));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glColorMask(mask[0],mask[1],mask[2],mask[3]);for(auto p:{std::pair<GLenum,bool>{GL_BLEND,blend},{GL_DEPTH_TEST,depth},{GL_SCISSOR_TEST,scissor},{GL_CULL_FACE,cull}})if(p.second)glEnable(p.first);else glDisable(p.first);}
};
}
void BloomRenderer::initialize(){if(vao_)return;
 const char*extract=BLOOM_SHADER R"(
uniform sampler2D sourceImage;uniform vec2 imageSize;uniform float weights[15];uniform float offsets[15];uniform float threshold;out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/imageSize;color=vec4(0);for(int i=0;i<15;++i){vec4 c=texture(sourceImage,uv+vec2(offsets[i]/imageSize.x,0));color+=max((c-threshold)/(1.-threshold),vec4(0))*weights[i];}})";
 const char*vertical=BLOOM_SHADER R"(
uniform sampler2D sourceImage;uniform vec2 imageSize;uniform float weights[15];uniform float offsets[15];out vec4 color;
void main(){vec2 uv=gl_FragCoord.xy/imageSize;color=vec4(0);for(int i=0;i<15;++i)color+=texture(sourceImage,uv+vec2(0,offsets[i]/imageSize.y))*weights[i];})";
 const char*combine=BLOOM_SHADER R"(
uniform sampler2D sourceImage;uniform sampler2D baseImage;uniform vec2 imageSize;uniform vec4 settings;out vec4 color;
vec3 saturation(vec3 c,float amount){return mix(vec3(dot(c,vec3(.3,.59,.11))),c,amount);}
void main(){vec2 uv=gl_FragCoord.xy/imageSize;vec3 bloom=saturation(texture(sourceImage,uv).rgb,settings.z)*settings.x;vec3 base=saturation(texture(baseImage,uv).rgb,settings.w);color=vec4(base*(1.-clamp(bloom,0.,1.))*settings.y+bloom,1.);})";
 try{programs_[0]=program(extract);programs_[1]=program(vertical);programs_[2]=program(combine);glGenVertexArrays(1,&vao_);glGenTextures(3,textures_.data());glGenFramebuffers(2,targets_.data());}catch(...){shutdown();throw;}
}
void BloomRenderer::resize(int w,int h){if(w==width_&&h==height_)return;for(auto t:textures_){glBindTexture(GL_TEXTURE_2D,t);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);}for(unsigned i=0;i<2;++i){glBindFramebuffer(GL_DRAW_FRAMEBUFFER,targets_[i]);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures_[i+1],0);if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Native bloom framebuffer incomplete");}width_=w;height_=h;}
void BloomRenderer::render(const FullscreenBloom&b,bool enabled,int w,int h){
 if(!b.shouldDraw(enabled))return;if(w<1||h<1||w>16384||h>16384)throw std::runtime_error("Native bloom viewport bounds");const auto&s=b.snapshot();if(s.threshold==1)throw std::runtime_error("Native bloom threshold singularity");const auto kernel=makeBloomKernel(s.blur);
 State saved;check("state query");initialize();check("initialization");glActiveTexture(GL_TEXTURE0);resize(w,h);check("target allocation");glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(saved.draw));glBindTexture(GL_TEXTURE_2D,textures_[0]);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,w,h);check("framebuffer copy");glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,w,h);glBindVertexArray(vao_);glBindSampler(0,0);glBindSampler(1,0);
 for(unsigned pass=0;pass<3;++pass){const auto p=programs_[pass];glBindFramebuffer(GL_DRAW_FRAMEBUFFER,pass<2?targets_[pass]:GLuint(saved.draw));glUseProgram(p);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures_[pass]);glUniform1i(glGetUniformLocation(p,"sourceImage"),0);glUniform2f(glGetUniformLocation(p,"imageSize"),float(w),float(h));if(pass<2){glUniform1fv(glGetUniformLocation(p,"weights"),15,kernel.weights.data());glUniform1fv(glGetUniformLocation(p,"offsets"),15,kernel.offsets.data());if(pass==0)glUniform1f(glGetUniformLocation(p,"threshold"),s.threshold);}else{glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,textures_[0]);glUniform1i(glGetUniformLocation(p,"baseImage"),1);glUniform4f(glGetUniformLocation(p,"settings"),s.bloomIntensity,s.baseIntensity,s.bloomSaturation,s.baseSaturation);}glDrawArrays(GL_TRIANGLES,0,3);check(pass==0?"horizontal":pass==1?"vertical":"combine");}
 if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native bloom GL draw failed");
}
void BloomRenderer::shutdown(){glDeleteFramebuffers(2,targets_.data());glDeleteTextures(3,textures_.data());if(vao_)glDeleteVertexArrays(1,&vao_);for(auto p:programs_)if(p)glDeleteProgram(p);targets_={};textures_={};programs_={};vao_=0;width_=height_=0;}
}
