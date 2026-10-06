/* Geometry inter-frame presentation. Included by gpu_gl_renderer.c so the
 * capture sites use the exact same shader inputs as the live mirror. Guest
 * VRAM is never a replay destination. The presenter owns its own GL objects;
 * published scenes and texture snapshots are immutable until its fence retires.
 */
#include "gpu_temporal_mesh.h"

#define TEMP_BUILD_SLOTS (WIDE_MAX_SURF * 2)
#define TEMP_MAX_VERTS 24576
#define TEMP_MAX_COMMANDS 2048
#define TEMP_MAX_TEXTURES 16
typedef struct {
    int first, count, first_tri, textured, mode, semi, mask, check, filter;
    int twin[4], sy, sh, texture;
} TemporalCommand;
typedef struct {
    GLuint capture_fbo;
    float *vertices;
    GpuTemporalTriangle *triangles;
    GpuTemporalTriangle *from;
    GpuTemporalMeshStats mesh_stats;
    TemporalCommand *commands;
    int *matches;
    int nv, nt, nc, active, bad, clear_y, clear_h, width, height, scale;
    int origin_y, texture_count, matched, world, moving;
    float clear[4], tint;
    GLuint textures[TEMP_MAX_TEXTURES];
    uint64_t texture_generation[TEMP_MAX_TEXTURES];
} TemporalScene;
static TemporalScene s_temp_build[TEMP_BUILD_SLOTS], s_temp_prev, s_temp_curr;
static SDL_atomic_t s_temp_worker_ready;
static uint64_t s_temp_published, s_temp_replays, s_temp_fallbacks;
static int s_temp_verify_left;
static int s_temp_last_matched, s_temp_last_world, s_temp_last_ready;
static GLuint s_temp_geo_prog, s_temp_tex_prog, s_temp_present_prog;
static GLuint s_temp_vao[2], s_temp_vbo, s_temp_fbo, s_temp_tex, s_temp_rb;
static int s_temp_width, s_temp_scale;
static float s_temp_scratch[TEXBATCH_MAXV * TEXV];
typedef struct {
    GLint shift, xhalf, visual, options, twin, mask, filter, pass, semi;
} TemporalUniforms;
static TemporalUniforms s_temp_uniforms[2];

static int temporal_recording(void) {
    return s_interp_enabled && s_interp_blend_mode == 2 &&
           !s_interp_suspended && s_geometry_correction && !s_ws_ablate;
}
static TemporalScene *temporal_builder(GLuint fbo, int y, int h) {
    for (int i=0;i<TEMP_BUILD_SLOTS;++i) {
        TemporalScene *s=&s_temp_build[i];
        if (fbo && s->capture_fbo==fbo && s->active &&
            s->clear_y<=y && s->clear_y+s->clear_h>=y+h) return s;
    }
    return NULL;
}
static void temporal_reset(void) {
    for (int i = 0; i < TEMP_BUILD_SLOTS; ++i) { s_temp_build[i].active = 0; s_temp_build[i].capture_fbo=0; }
    s_temp_prev.active = s_temp_curr.active = 0;
}
static int temporal_allocate(TemporalScene *s) {
    if (!s->vertices) s->vertices = (float *)malloc(TEMP_MAX_VERTS * TEXV * sizeof(float));
    if (!s->triangles) s->triangles = (GpuTemporalTriangle *)malloc((TEMP_MAX_VERTS/3) * sizeof(GpuTemporalTriangle));
    if (!s->from) s->from = (GpuTemporalTriangle *)malloc((TEMP_MAX_VERTS/3) * sizeof(GpuTemporalTriangle));
    if (!s->commands) s->commands = (TemporalCommand *)malloc(TEMP_MAX_COMMANDS * sizeof(TemporalCommand));
    if (!s->matches) s->matches = (int *)malloc((TEMP_MAX_VERTS/3) * sizeof(int));
    return s->vertices && s->triangles && s->from && s->commands && s->matches;
}
static void temporal_begin(GLuint fbo, int y, int h, uint16_t color) {
    TemporalScene *s=NULL;
    for (int i=0;i<TEMP_BUILD_SLOTS;++i)
        if (s_temp_build[i].capture_fbo==fbo && s_temp_build[i].clear_y==y) { s=&s_temp_build[i]; break; }
    if (!s) for (int i=0;i<TEMP_BUILD_SLOTS;++i)
        if (!s_temp_build[i].active) { s=&s_temp_build[i]; break; }
    if (!s) return;
    for (int i=0;i<TEMP_BUILD_SLOTS;++i) {
        TemporalScene *other=&s_temp_build[i];
        if (other!=s && other->capture_fbo==fbo && other->active &&
            other->clear_y<y+h && y<other->clear_y+other->clear_h)
            other->active=0;
    }
    s->capture_fbo=fbo;
    s->active = 0;
    if (!temporal_recording() || !temporal_allocate(s)) return;
    s->active = 1; s->bad = 0; s->nv = s->nt = s->nc = 0;
    s->texture_count = 0; s->clear_y = y; s->clear_h = h;
    s->width = g_wide_w; s->scale = s_scale;
    s->clear[0] = (color & 31) / 31.0f;
    s->clear[1] = ((color >> 5) & 31) / 31.0f;
    s->clear[2] = ((color >> 10) & 31) / 31.0f;
    s->clear[3] = (color & 0x8000) ? 1.0f : 0.0f;
    s->tint = s_geometry_coverage_tint ? 0.55f : 0.0f;
}
static void temporal_invalidate_surface(GLuint fbo) {
    for (int i=0;i<TEMP_BUILD_SLOTS;++i)
        if (s_temp_build[i].capture_fbo==fbo) s_temp_build[i].bad=4;
}

/* Snapshot every distinct texture generation used by this scene. This also
 * handles texture uploads between draws, without sampling mutable VRAM from
 * the other context. A bounded overflow holds the real frame. */
static int temporal_texture(TemporalScene *s) {
    for (int i = 0; i < s->texture_count; ++i)
        if (s->texture_generation[i] == s_temporal_raw_generation) return i;
    if (s->texture_count == TEMP_MAX_TEXTURES) { s->bad = 3; return -1; }
    int i = s->texture_count++;
    GLint read_fbo = 0, texture = 0;
    glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &read_fbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (!s->textures[i])
        s->textures[i] = make_tex(PSXGL_R16UI, VRAM_W, VRAM_H,
                                  PSXGL_RED_INTEGER, GL_UNSIGNED_SHORT);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, s_raw_fbo);
    glBindTexture(GL_TEXTURE_2D, s->textures[i]);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, VRAM_W, VRAM_H);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, (GLuint)read_fbo);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
    s->texture_generation[i] = s_temporal_raw_generation;
    return i;
}
static uint64_t temporal_hash(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}

/* Bake the mirror's projection into local screen coordinates. Both the
 * canonical and precise slots get the baked value; texture perspective and
 * coverage flags remain exactly those selected for the original draw. */
static void temporal_position(float *v, int textured, int gate, int direct) {
    int flags_at = textured ? 19 : 6, pos_at = textured ? 20 : 7;
    int corrected = !direct && s_geometry_correction && v[flags_at] > 0.5f;
    float x = corrected ? v[pos_at] : v[0], y = corrected ? v[pos_at+1] : v[1];
    if (corrected && fabsf(s_yaw_sin) > 0.0000001f && s_yaw_focal_x > 0) {
        float cx = g_wide_cur_base + s_yaw_center_x;
        float cy = s_off_y + s_yaw_center_y_relative;
        float ux = (x - cx) / s_yaw_focal_x;
        float den = s_yaw_cos - ux * s_yaw_sin;
        if (fabsf(den) > 0.01f) {
            x = cx + s_yaw_focal_x * (ux*s_yaw_cos+s_yaw_sin) / den;
            if (s_geometry_full_yaw) y = cy + (y-cy)/den;
        }
    }
    if (textured && (!corrected || v[flags_at] <= 1.5f || fabsf(s_yaw_sin) > 0.0000001f))
        v[22] = 0;
    if (!direct) {
        if (gate && g_ws_bd_stretch_on) {
            extern int g_ws_tex_edge_pct;
            int nw = g_wide_w - 2*g_wide_off;
            if (nw > 0) {
                float factor = g_ws_bd_stretch_pct > 0 ? g_ws_bd_stretch_pct/100.0f : (float)g_wide_w/nw;
                float cx = g_wide_cur_base + nw/2.0f;
                if (gate == 2) {
                    factor = g_ws_tex_edge_pct > 0 ? g_ws_tex_edge_pct/100.0f : factor;
                    float half = (g_wide_w/2.0f)/factor;
                    if (x < cx-half) x = cx-half+(x-(cx-half))*factor;
                    else if (x > cx+half) x = cx+half+(x-(cx+half))*factor;
                } else x = (x-cx)*factor+cx;
            }
        }
        x += wide_dx();
    }
    v[0] = v[pos_at] = x; v[1] = v[pos_at+1] = y;
}
static void temporal_draw(const float *vertices, int n, int textured,
                          GLenum mode, int semi, int mask, int filter,
                          const int *twin, int gate, int direct) {
    if (!temporal_recording()) return;
    TemporalScene *s = temporal_builder(g_wide_cur, s_area_y1, s_area_y2-s_area_y1+1);
    if (!s) {
        /* An unretained draw touching a captured band makes that band unsafe.
         * Do not publish an incomplete command list as a valid scene. */
        for (int i=0;i<TEMP_BUILD_SLOTS;++i) {
            TemporalScene *other=&s_temp_build[i];
            if (other->active && other->capture_fbo==g_wide_cur &&
                other->clear_y<=s_area_y2 && s_area_y1<other->clear_y+other->clear_h)
                other->bad=5;
        }
        return;
    }
    if (!s->active || s->bad) return;
    /* Deferred destination-mask reconstruction is not replayed yet. Reject
     * the entire scene rather than inventing stencil state between batches. */
    if (s_mask_check) { s->bad=1; return; }
    if (s->nc == TEMP_MAX_COMMANDS || s->nv+n > TEMP_MAX_VERTS ||
        n > TEXBATCH_MAXV || (mode != GL_TRIANGLES && mode != GL_LINES)) { s->bad=2; return; }
    TemporalCommand *c = &s->commands[s->nc++];
    memset(c, 0, sizeof(*c));
    c->first=s->nv; c->count=n; c->first_tri=s->nt;
    c->textured=textured; c->mode=(int)mode; c->semi=semi;
    c->mask=mask; c->check=s_mask_check; c->filter=filter;
    c->sy=direct ? 0 : s_area_y1; c->sh=direct ? VRAM_H : s_area_y2-s_area_y1+1;
    if (twin) memcpy(c->twin, twin, sizeof(c->twin));
    c->texture=textured ? temporal_texture(s) : -1;
    if (s->bad) return;
    const int stride = textured ? TEXV : GEOV;
    for (int i=0; i<n; ++i) {
        float *v = &s->vertices[(s->nv+i)*TEXV];
        memset(v, 0, TEXV*sizeof(float));
        memcpy(v, vertices+i*stride, stride*sizeof(float));
        temporal_position(v, textured, gate, direct);
    }
    if (mode == GL_TRIANGLES) for (int i=0; i<n; i+=3) {
        GpuTemporalTriangle *t=&s->triangles[s->nt++];
        memset(t,0,sizeof(*t));
        uint64_t hash=UINT64_C(14695981039346656037);
        hash=temporal_hash(hash,&textured,sizeof(textured));
        hash=temporal_hash(hash,&c->semi,sizeof(c->semi));
        hash=temporal_hash(hash,&c->mask,sizeof(c->mask));
        hash=temporal_hash(hash,&c->filter,sizeof(c->filter));
        hash=temporal_hash(hash,c->twin,sizeof(c->twin));
        t->world = 1;
        for (int j=0; j<3; ++j) {
            float *v=&s->vertices[(s->nv+i+j)*TEXV];
            t->x[j]=v[0]; t->y[j]=v[1]; t->q[j]=textured ? v[22] : 0;
            t->depth[j]=v[textured ? 23 : 9];
            if (v[textured ? 19 : 6] <= 0.5f) t->world=0;
            if (textured) {
                hash=temporal_hash(hash,v+2,2*sizeof(float));
                hash=temporal_hash(hash,v+8,11*sizeof(float));
            } else hash=temporal_hash(hash,v+2,4*sizeof(float));
        }
        t->material=hash;
    }
    s->nv+=n;
}

/* Optional single moving-scene capture for checking actual game topology.
 * Absent from normal builds/play; no readback or file I/O unless requested. */
#ifndef PSX_NO_DEBUG_TOOLS
static void temporal_dump_mesh(void) {
    static int done;
    const char *path;
    FILE *file;
    int moved=0;
    if (done || s_temp_curr.world<50 || !s_temp_curr.moving) return;
    path=getenv("PSX_GL_TEMPORAL_MESH_DUMP");
    if (!path || !path[0]) return;
    for (int i=0;i<s_temp_curr.nt && !moved;++i)
        for (int j=0;j<3;++j)
            if (fabsf(s_temp_curr.from[i].x[j]-s_temp_curr.triangles[i].x[j])>1 ||
                fabsf(s_temp_curr.from[i].y[j]-s_temp_curr.triangles[i].y[j])>1) moved=1;
    if (!moved) return;
    done=1;
    file=fopen(path,"wb");
    if (!file) return;
    fprintf(file,"{\"matches\":[");
    for (int i=0;i<s_temp_curr.nt;++i) fprintf(file,"%s%d",i?",":"",s_temp_curr.matches[i]);
    fprintf(file,"]");
    for (int set=0;set<3;++set) {
        const GpuTemporalTriangle *tri=set==0?s_temp_prev.triangles:set==1?s_temp_curr.triangles:s_temp_curr.from;
        int n=set==0?s_temp_prev.nt:s_temp_curr.nt;
        fprintf(file,",\"%s\":[",set==0?"previous":set==1?"current":"from");
        for (int i=0;i<n;++i) {
            fprintf(file,"%s{\"world\":%d,\"vertices\":[",i?",":"",tri[i].world);
            for (int j=0;j<3;++j)
                fprintf(file,"%s[%.9g,%.9g,%.9g,%.9g]",j?",":"",
                        tri[i].x[j],tri[i].y[j],tri[i].depth[j],tri[i].q[j]);
            fprintf(file,"]}");
        }
        fprintf(file,"]");
    }
    fprintf(file,"}\n");
    fclose(file);
    fprintf(stdout,"psxrecomp: GL geometry moving mesh captured\n");
}
#endif

/* Called with the present mutex locked, after the worker's last draw fence.
 * Rotate allocations rather than copying scene-sized buffers. */
static void temporal_publish(GLuint fbo, int y, int w, int h) {
    TemporalScene *s=temporal_builder(fbo,y,h);
    if (!temporal_recording() || !s || !s->active || s->bad ||
        s->clear_y > y || s->clear_y+s->clear_h < y+h ||
        s->width != w || s->scale != s_scale || !s->nc) {
        if (s_interp_diag && s_temp_fallbacks<12)
            fprintf(stdout,"psxrecomp: GL geometry fallback: builder=%d active=%d reason=%d commands=%d clear=%d+%d display=%d+%d\n",
                    s!=NULL,s?s->active:0,s?s->bad:0,s?s->nc:0,s?s->clear_y:0,s?s->clear_h:0,y,h);
        s_temp_prev.active=s_temp_curr.active=0;
        if (s) s->active=0;
        s_temp_last_matched=s_temp_last_world=s_temp_last_ready=0;
        ++s_temp_fallbacks;
        return;
    }
    s->origin_y=y; s->height=h; s->matched=s->world=s->moving=0;
    memset(&s->mesh_stats,0,sizeof(s->mesh_stats));
    for (int i=0; i<s->nc; ++i) {
        TemporalCommand *c=&s->commands[i];
        c->sy-=y;
        for (int j=0; j<c->count; ++j) {
            float *v=&s->vertices[(c->first+j)*TEXV];
            v[1]-=y; v[c->textured ? 21 : 8]-=y;
        }
    }
    for (int i=0; i<s->nt; ++i) {
        for (int j=0; j<3; ++j) s->triangles[i].y[j]-=y;
        s->matches[i]=-1;
        if (s->triangles[i].world) ++s->world;
    }
    memcpy(s->from,s->triangles,s->nt*sizeof(*s->from));
    TemporalScene old=s_temp_prev;
    s_temp_prev=s_temp_curr; s_temp_curr=*s; *s=old; s->active=0;
    s->capture_fbo=fbo; s->clear_y=s_temp_curr.clear_y;
    if (s_temp_prev.active && s_temp_prev.width==w && s_temp_prev.height==h &&
        s_temp_prev.scale==s_scale && s_temp_prev.tint==s_temp_curr.tint) {
        gpu_temporal_match(s_temp_prev.triangles,s_temp_prev.nt,
                           s_temp_curr.triangles,s_temp_curr.nt,s_temp_curr.matches);
        for (int i=0; i<s_temp_curr.nt; ++i)
            if (s_temp_curr.matches[i]>=0) ++s_temp_curr.matched;
        s_temp_curr.moving=gpu_temporal_mesh_prepare(
            s_temp_prev.triangles,s_temp_prev.nt,s_temp_curr.triangles,
            s_temp_curr.nt,s_temp_curr.matches,s_temp_curr.from,
            &s_temp_curr.mesh_stats);
    }
    s_temp_last_matched=s_temp_curr.matched;
    s_temp_last_world=s_temp_curr.world;
    s_temp_last_ready=s_temp_curr.moving>0;
#ifndef PSX_NO_DEBUG_TOOLS
    temporal_dump_mesh();
#endif
    ++s_temp_published;
}

static void temporal_uniforms_init(GLuint p, TemporalUniforms *u) {
#define TEMP_U(field,name) u->field=p_glGetUniformLocation(p,name)
    TEMP_U(shift,"u_shift"); TEMP_U(xhalf,"u_xhalf"); TEMP_U(visual,"u_visual");
    TEMP_U(options,"u_visual_options"); TEMP_U(twin,"u_twin"); TEMP_U(mask,"u_maskset");
    TEMP_U(filter,"u_filter"); TEMP_U(pass,"u_semipass"); TEMP_U(semi,"u_semimode");
#undef TEMP_U
    p_glUseProgram(p);
    p_glUniform1f(p_glGetUniformLocation(p,"u_xscale"),1);
    p_glUniform4f(p_glGetUniformLocation(p,"u_camera"),0,1,0,0);
    p_glUniform1i(p_glGetUniformLocation(p,"u_vram"),0);
}
static int temporal_worker_init(void) {
    const char *verify=getenv("PSX_GL_TEMPORAL_VERIFY");
    s_temp_verify_left=verify && verify[0]=='1' ? 3 : 0;
    s_temp_geo_prog=build_program(GEO_VS,GEO_FS);
    s_temp_tex_prog=build_program_ex(TEX_VS,TEX_FS,1);
    s_temp_present_prog=build_program(PRESENT_VS,PRESENT_FS);
    if (!s_temp_geo_prog || !s_temp_tex_prog || !s_temp_present_prog) return 0;
    temporal_uniforms_init(s_temp_geo_prog,&s_temp_uniforms[0]);
    temporal_uniforms_init(s_temp_tex_prog,&s_temp_uniforms[1]);
    p_glGenBuffers(1,&s_temp_vbo);
    p_glGenVertexArrays(2,s_temp_vao);
    const int sizes[12]={2,2,4,2,2,1,1,4,1,1,2,1};
    const int offsets[12]={0,2,4,8,10,12,13,14,18,19,20,22};
    for (int t=0;t<2;++t) {
        p_glBindVertexArray(s_temp_vao[t]);
        p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_temp_vbo);
        for (int a=0;a<(t?12:4);++a) {
            const int gs[4]={2,4,1,2}, go[4]={0,2,6,7};
            p_glVertexAttribPointer(a,t?sizes[a]:gs[a],GL_FLOAT,GL_FALSE,
                TEXV*sizeof(float),(void *)(size_t)((t?offsets[a]:go[a])*sizeof(float)));
            p_glEnableVertexAttribArray(a);
        }
    }
    p_glBindVertexArray(0); p_glUseProgram(0);
    return 1;
}
static int temporal_target(int width,int scale) {
    if (s_temp_fbo && s_temp_width==width && s_temp_scale==scale) return 1;
    if (s_temp_fbo) p_glDeleteFramebuffers(1,&s_temp_fbo);
    if (s_temp_tex) glDeleteTextures(1,&s_temp_tex);
    if (s_temp_rb) p_glDeleteRenderbuffers(1,&s_temp_rb);
    s_temp_fbo=s_temp_tex=s_temp_rb=0;
    s_temp_tex=make_tex(GL_RGBA8,width*scale,VRAM_H*scale,GL_RGBA,GL_UNSIGNED_BYTE);
    p_glGenRenderbuffers(1,&s_temp_rb);
    p_glBindRenderbuffer(PSXGL_RENDERBUFFER,s_temp_rb);
    p_glRenderbufferStorage(PSXGL_RENDERBUFFER,PSXGL_DEPTH24_STENCIL8,width*scale,VRAM_H*scale);
    if (!make_fbo(&s_temp_fbo,s_temp_tex,s_temp_rb)) return 0;
    s_temp_width=width; s_temp_scale=scale;
    return 1;
}
static void temporal_mask(int check,int value) {
    glEnable(GL_STENCIL_TEST); glStencilMask(1);
    glStencilFunc(check ? GL_EQUAL : GL_ALWAYS,check ? 0 : value,1);
    glStencilOp(GL_KEEP,GL_KEEP,check ? (value ? GL_INVERT : GL_KEEP) : GL_REPLACE);
}
static int temporal_present(float alpha,int lx,int ly,int lw,int lh) {
    TemporalScene *s=&s_temp_curr;
    if (!s->active || !s->moving ||
        !temporal_target(s->width,s->scale)) return 0;
    if (s_interp_fence[s_interp_cur]) p_glWaitSync(s_interp_fence[s_interp_cur],0,PSXGL_TIMEOUT_IGNORED);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_temp_fbo);
    glViewport(0,0,s->width*s->scale,VRAM_H*s->scale);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glStencilMask(0xFF); glClearStencil(s->clear[3]>0.5f ? 1:0);
    glClearColor(s->clear[0],s->clear[1],s->clear[2],s->clear[3]);
    glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    p_glActiveTexture(PSXGL_TEXTURE0);
    for (int i=0;i<s->nc;++i) {
        TemporalCommand *c=&s->commands[i];
        const int t=c->textured;
        TemporalUniforms *u=&s_temp_uniforms[t];
        memcpy(s_temp_scratch,s->vertices+c->first*TEXV,c->count*TEXV*sizeof(float));
        if (c->mode==GL_TRIANGLES) for (int j=0;j<c->count;j+=3) {
            int ti=c->first_tri+j/3;
            if (!s->triangles[ti].world) continue;
            GpuTemporalTriangle out;
            gpu_temporal_lerp(&s->from[ti],&s->triangles[ti],alpha,&out);
            for (int k=0;k<3;++k) {
                float *v=&s_temp_scratch[(j+k)*TEXV];
                v[0]=v[t?20:7]=out.x[k]; v[1]=v[t?21:8]=out.y[k];
                if (t) v[22]=out.q[k];
            }
        }
        int sy=c->sy, sh=c->sh;
        if (sy<0) { sh+=sy;sy=0; }
        if (sy+sh>s->height) sh=s->height-sy;
        if (sh<=0) continue;
        glEnable(GL_SCISSOR_TEST); glScissor(0,sy*s->scale,s->width*s->scale,sh*s->scale);
        p_glUseProgram(t?s_temp_tex_prog:s_temp_geo_prog);
        p_glUniform1f(u->shift,0.5f/s->scale-1.0f/64.0f);
        p_glUniform1f(u->xhalf,s->width/2.0f); p_glUniform1f(u->visual,1);
        p_glUniform4f(u->options,0,0,s->tint,0);
        p_glBindVertexArray(s_temp_vao[t]); p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_temp_vbo);
        p_glBufferData(PSXGL_ARRAY_BUFFER,c->count*TEXV*sizeof(float),s_temp_scratch,PSXGL_STREAM_DRAW);
        if (!t) {
            if (c->semi>=0) apply_psx_blend(c->semi); else glDisable(GL_BLEND);
            temporal_mask(c->check,c->mask);
            glLineWidth((float)s->scale);
            glDrawArrays((GLenum)c->mode,0,c->count);
        } else {
            glBindTexture(GL_TEXTURE_2D,s->textures[c->texture]);
            p_glUniform4i(u->twin,c->twin[0],c->twin[1],c->twin[2],c->twin[3]);
            p_glUniform1i(u->mask,c->mask); p_glUniform1i(u->filter,c->filter);
            p_glUniform1i(u->semi,c->semi<0?0:c->semi);
            if (!c->check && c->semi==4) {
                glEnable(GL_BLEND);
                p_glBlendEquationSeparate(PSXGL_FUNC_ADD,PSXGL_FUNC_ADD);
                p_glBlendFuncSeparate(GL_ONE,PSXGL_SRC1_ALPHA,GL_ONE,GL_ZERO);
                temporal_mask(0,c->mask);
                p_glUniform1i(u->pass,0); glDrawArrays(GL_TRIANGLES,0,c->count);
            } else if (c->semi<0) {
                glDisable(GL_BLEND); temporal_mask(c->check,c->mask);
                p_glUniform1i(u->pass,0); glDrawArrays(GL_TRIANGLES,0,c->count);
            } else {
                glDisable(GL_BLEND); temporal_mask(c->check,c->mask);
                p_glUniform1i(u->pass,1); glDrawArrays(GL_TRIANGLES,0,c->count);
                apply_psx_blend(c->semi); temporal_mask(c->check,1);
                p_glUniform1i(u->pass,2); glDrawArrays(GL_TRIANGLES,0,c->count);
            }

        }
    }
    glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,0); glViewport(lx,ly,lw,lh);
    glBindTexture(GL_TEXTURE_2D,s_temp_tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,s_interp_linear?GL_LINEAR:GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,s_interp_linear?GL_LINEAR:GL_NEAREST);
    p_glUseProgram(s_temp_present_prog);
    p_glUniform1i(p_glGetUniformLocation(s_temp_present_prog,"u_tex"),0);
    p_glUniform4f(p_glGetUniformLocation(s_temp_present_prog,"u_uv_rect"),
                  0.5f/s->width,0.5f/VRAM_H,
                  1-0.5f/s->width,(s->height-0.5f)/VRAM_H);
    p_glBindVertexArray(s_interp_thread_vao); glDrawArrays(GL_TRIANGLES,0,3);
    p_glBindVertexArray(0); p_glUseProgram(0);
    ++s_temp_replays;
    return 1;
}
/* Optional bounded GPU parity check. Compare the rerasterized alpha=1
 * endpoint against the real source, before display filtering. Readbacks are
 * deliberately absent from normal play. */
static void temporal_verify_endpoint(int lx,int ly,int lw,int lh) {
    if (!s_temp_verify_left || !s_temp_curr.active || !s_temp_curr.moving) return;
    --s_temp_verify_left;
    if (!temporal_present(1,lx,ly,lw,lh)) return;
    size_t pixels=(size_t)s_interp_w*s_interp_h;
    unsigned char *actual=(unsigned char *)malloc(pixels*8);
    if (!actual) return;
    unsigned char *expected=actual+pixels*4;
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s_temp_fbo);
    glReadPixels(0,0,s_interp_w,s_interp_h,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    glBindTexture(GL_TEXTURE_2D,s_interp_tex[s_interp_cur]);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,expected);
    size_t different=0; int max_delta=0;
    for (size_t i=0;i<pixels;++i) {
        int changed=0;
        for (int c=0;c<3;++c) {
            int d=abs((int)actual[4*i+c]-(int)expected[4*i+c]);
            if (d>max_delta) max_delta=d;
            if (d>1) changed=1;
        }
        different+=changed;
    }
    fprintf(stdout,"psxrecomp: GL geometry endpoint check: %llu/%llu RGB pixels differ >1, max delta %d, GL error 0x%x\n",
            (unsigned long long)different,(unsigned long long)pixels,max_delta,(unsigned)glGetError());
    fflush(stdout);
    free(actual);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,0);
}
static void temporal_release(void) {
    /* Main context, only after the worker has stopped. Shared programs/buffers
     * disappear with the share group; free CPU allocations and texture pools. */
    for (int i=0;i<TEMP_BUILD_SLOTS+2;++i) {
        TemporalScene *s=i<TEMP_BUILD_SLOTS ? &s_temp_build[i] : i==TEMP_BUILD_SLOTS ? &s_temp_prev : &s_temp_curr;
        free(s->vertices); free(s->triangles); free(s->from); free(s->commands); free(s->matches);
        glDeleteTextures(TEMP_MAX_TEXTURES,s->textures);
        memset(s,0,sizeof(*s));
    }
    s_temp_fbo=s_temp_tex=s_temp_rb=0; s_temp_width=s_temp_scale=0;
}
