#include "color_grading_renderer.hpp"
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define CG_SHADER "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#define CG_SHADER "#version 330 core\n"
#endif
#include <stdexcept>
namespace hades {
namespace {
unsigned shader(GLenum kind,const char*text){const auto id=glCreateShader(kind);glShaderSource(id,1,&text,nullptr);glCompileShader(id);int ok=0;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){char error[2048]{};glGetShaderInfoLog(id,sizeof(error),nullptr,error);glDeleteShader(id);throw std::runtime_error(std::string("Native color grading shader: ")+error);}return id;}
}
void ColorGradingRenderer::initialize(){
 if(program_)return;
 const char*vertex=CG_SHADER R"(
 void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));gl_Position=vec4(p*2.-1.,0.,1.);})";
 // Original ColorGrading.frag / ColorGradingLerp.frag algebra. Each LUT uses
 // its own dimension, including source16^3 and32^3 transitions.
 const char*fragment=CG_SHADER R"(
 uniform sampler2D sourceImage;uniform sampler3D previousLut;uniform sampler3D currentLut;
 uniform float previousSize;uniform float currentSize;uniform float fraction;
 uniform bool hasPrevious;uniform bool hasCurrent;
 out vec4 outputColor;
 vec4 grade(sampler3D lut,float size,vec3 raw){return texture(lut,vec3(1./(2.*size))+raw*((size-1.)/size));}
 void main(){vec4 raw=texelFetch(sourceImage,ivec2(gl_FragCoord.xy),0);vec4 a=raw,b=raw;
 if(hasPrevious)a=grade(previousLut,previousSize,raw.rgb);
 if(hasCurrent)b=grade(currentLut,currentSize,raw.rgb);
 outputColor=mix(a,b,fraction);})";
 const auto v=shader(GL_VERTEX_SHADER,vertex);unsigned f=0;try{f=shader(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(v);throw;}
 program_=glCreateProgram();glAttachShader(program_,v);glAttachShader(program_,f);glLinkProgram(program_);glDeleteShader(v);glDeleteShader(f);int ok=0;glGetProgramiv(program_,GL_LINK_STATUS,&ok);if(!ok){char error[2048]{};glGetProgramInfoLog(program_,sizeof(error),nullptr,error);shutdown();throw std::runtime_error(std::string("Native color grading link: ")+error);}
 glGenVertexArrays(1,&vao_);glGenTextures(1,&source_);
}
unsigned ColorGradingRenderer::upload(size_t slot,const ColorVolume*volume){
 if(!volume||volume->size<2||volume->size>64||volume->rgba.size()!=uint64_t(volume->size)*volume->size*volume->size*4)throw std::runtime_error("Invalid original GL color volume");
 auto&v=volumes_.at(slot);if(v.volume==volume&&v.texture){glBindTexture(GL_TEXTURE_3D,v.texture);return v.texture;}
 if(!v.texture)glGenTextures(1,&v.texture);glBindTexture(GL_TEXTURE_3D,v.texture);
 glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 for(auto axis:{GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_WRAP_R})glTexParameteri(GL_TEXTURE_3D,axis,GL_CLAMP_TO_EDGE);
 glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,volume->size,volume->size,volume->size,0,GL_RGBA,GL_UNSIGNED_BYTE,volume->rgba.data());v.volume=volume;return v.texture;
}
void ColorGradingRenderer::render(const ColorGrading&state,int width,int height){
 if(!state.active())return;if(width<1||height<1||width>16384||height>16384)throw std::runtime_error("Native grading viewport bounds");
 GLint savedProgram=0,savedVao=0,savedActive=0,savedRead=0,draw=0;std::array<GLint,4>viewport{};
 glGetIntegerv(GL_CURRENT_PROGRAM,&savedProgram);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&savedVao);glGetIntegerv(GL_ACTIVE_TEXTURE,&savedActive);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&savedRead);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw);glGetIntegerv(GL_VIEWPORT,viewport.data());const bool blend=glIsEnabled(GL_BLEND);
 initialize();glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 if(width_!=width||height_!=height){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);width_=width;height_=height;}
 glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(draw));glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(savedRead));
 const auto*previous=state.previous()?state.previous():state.current();const auto*current=state.current()?state.current():previous;
 glActiveTexture(GL_TEXTURE1);upload(0,previous);glActiveTexture(GL_TEXTURE2);upload(1,current);
 glUseProgram(program_);glBindVertexArray(vao_);glDisable(GL_BLEND);glViewport(0,0,width,height);
 glUniform1i(glGetUniformLocation(program_,"sourceImage"),0);glUniform1i(glGetUniformLocation(program_,"previousLut"),1);glUniform1i(glGetUniformLocation(program_,"currentLut"),2);
 glUniform1i(glGetUniformLocation(program_,"hasPrevious"),state.previous()!=nullptr);glUniform1i(glGetUniformLocation(program_,"hasCurrent"),state.current()!=nullptr);
 glUniform1f(glGetUniformLocation(program_,"previousSize"),float(previous->size));glUniform1f(glGetUniformLocation(program_,"currentSize"),float(current->size));glUniform1f(glGetUniformLocation(program_,"fraction"),state.fraction());glDrawArrays(GL_TRIANGLES,0,3);
 glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_3D,0);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_3D,0);glActiveTexture(GLenum(savedActive));glUseProgram(GLuint(savedProgram));glBindVertexArray(GLuint(savedVao));if(blend)glEnable(GL_BLEND);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
 if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native 3D color grading GL draw failed");
}
void ColorGradingRenderer::shutdown(){for(auto&v:volumes_){if(v.texture)glDeleteTextures(1,&v.texture);v={};}if(source_)glDeleteTextures(1,&source_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);source_=vao_=program_=0;width_=height_=0;}
}
