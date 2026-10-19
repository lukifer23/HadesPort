/* Offline original Bink planar-frame conversion through existing libswscale. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "libswscale/swscale.h"
#include "libavutil/pixfmt.h"
static uint32_t u32(const unsigned char*p){return p[0]|p[1]<<8|p[2]<<16|p[3]<<24;}
int main(int argc,char**argv){if(argc!=3)return 2;FILE*f=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");unsigned char header[12];if(!f||!out||fread(header,1,12,f)!=12||memcmp(header,"NIH1",4))return 3;const int w=u32(header+4),h=u32(header+8);if(w<1||h<1||w>2048||h>2048)return 4;const int cw=(w+1)/2,ch=(h+1)/2;size_t yy=(size_t)w*h,cc=(size_t)cw*ch,total=2*yy+2*cc;uint8_t*frame=malloc(total),*rgba=malloc(4*yy);if(!frame||!rgba)return 5;struct SwsContext*s=sws_getContext(w,h,AV_PIX_FMT_YUVA420P,w,h,AV_PIX_FMT_RGBA,SWS_BICUBIC,NULL,NULL,NULL);if(!s||sws_setColorspaceDetails(s,sws_getCoefficients(SWS_CS_ITU601),1,sws_getCoefficients(SWS_CS_ITU601),1,0,1<<16,1<<16)<0)return 6;unsigned frames=0;while(1){size_t got=fread(frame,1,total,f);if(!got)break;if(got!=total)return 7;const uint8_t*planes[]={frame,frame+yy,frame+yy+cc,frame+yy+2*cc};const int strides[]={w,cw,cw,w};uint8_t*dest[4]={rgba,NULL,NULL,NULL};const int dststrides[4]={w*4,0,0,0};if(sws_scale(s,planes,strides,0,h,dest,dststrides)!=h||fwrite(rgba,1,4*yy,out)!=4*yy)return 8;++frames;}sws_freeContext(s);free(frame);free(rgba);fclose(f);fclose(out);fprintf(stderr,"Converted %u original frames %dx%d fullrange601\n",frames,w,h);return 0;}
