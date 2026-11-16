/* Private legacy mode: sourced from v2.4.6 (569f2d4), with pitch fixed at zero.
 * Included beside its dispatcher to retain the original private renderer state.
 * Experimental changes belong in the other path, never in this reference. */
static void RenderPostBandLegacy246(int y0,int y1) {
    float stress=perception.stress,crisis=perception.contamination;
    float beat=.5f+.5f*sinf((float)g_timeSec*6.0f);
    int radius=1+(int)(stress*3),dark=(int)(256*perception.vignette);
    int doll=(ArtStyleDoll()&&craftTex!=NULL);
    for(int y=y0;y<y1;y++){
        unsigned int* row=pixels+(size_t)y*renderW;
        uint32_t noise=perception.seed^(uint32_t)(y*7919+107);
        int wobble=(int)(sinf(y*.075f+(float)g_timeSec*1.9f)*perception.distortion*renderW);
        int veinOffset=(int)(sinf(y*.037f)*36)+(int)(perception.seed%9)*3;
        for(int x=0;x<renderW;x++){
            unsigned int c=row[x];float edge=vignetteMask?vignetteMask[(size_t)y*renderW+x]/255.0f:0;
            int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            if(horrorReady){
                int sx=(x+wobble)/2;if(sx<radius)sx=radius;if(sx>=horrorW-radius)sx=horrorW-radius-1;
                int sy=y/2;if(sy<radius)sy=radius;if(sy>=horrorH-radius)sy=horrorH-radius-1;
                unsigned int a=horrorSource[(size_t)sy*horrorW+sx-radius],d=horrorSource[(size_t)sy*horrorW+sx+radius];
                unsigned int u=horrorSource[(size_t)(sy-radius)*horrorW+sx],v=horrorSource[(size_t)(sy+radius)*horrorW+sx];
                float blur=stress*.68f;
                r=(int)(r*(1-blur)+blur*(((a>>16)&255)+((d>>16)&255)+((u>>16)&255)+((v>>16)&255))*.25f);
                g=(int)(g*(1-blur)+blur*(((a>>8)&255)+((d>>8)&255)+((u>>8)&255)+((v>>8)&255))*.25f);
                b=(int)(b*(1-blur)+blur*((a&255)+(d&255)+(u&255)+(v&255))*.25f);
            }
            noise=noise*1664525u+1013904223u;
            int speck=(int)((((int)(noise>>24)-128)/128.0f)*perception.grain);
            float blood=vignetteOn?(stress*edge*.52f+crisis*(.10f+beat*.12f)):0;
            float vein=((x+veinOffset+128)%57<2 && (noise&128))?crisis*edge*.6f:0;
            if(vignetteOn && stress>.12f && edge>.48f){
                /* Red/cyan ink separation on the border; preserve central navigation. */
                int ink=((x/11+y/23)%7==0)?(int)(stress*14):0;
                r+=ink;b+=ink/3;g-=ink/2;
            }
            float shade=1-edge*dark/256.0f;
            r=(int)(r*shade*(1-blood)+145*blood-45*vein)+speck;
            g=(int)(g*shade*(1-blood*.9f)-32*vein)+speck;
            b=(int)(b*shade*(1-blood*.85f)-28*vein)+speck;
            if(r<0)r=0;
            if(r>255)r=255;
            if(g<0)g=0;
            if(g>255)g=255;
            if(b<0)b=0;
            if(b>255)b=255;
            if(doll){
                /* 2026-11-11：布偶整屏层。只乘织纹/渐晕，不做颜色分级
                   （分级已在贴图与精灵烘焙期完成）。 */
                int k=craftTex[(size_t)y*renderW+x];
                r=(r*k)>>7;if(r>255)r=255;
                g=(g*k)>>7;if(g>255)g=255;
                b=(b*k)>>7;if(b>255)b=255;
            }
            row[x]=(unsigned)(r<<16|g<<8|b);
        }
        if(doll)CraftFrameRow(y,row);
    }
}
