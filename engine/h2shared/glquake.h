/*
 * glquake.h -- common glquake header
 * $Id$
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 2005-2012  O.Sezer <sezero@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef GLQUAKE_H
#define GLQUAKE_H

/* ====================================================================
   COMMON DEFINITIONS
   ================================================================== */

#define MAX_GLTEXTURES		2048
#define MAX_EXTRA_TEXTURES	156	/* 255-100+1 */
#define	MAX_CACHED_PICS		256
#define	MAX_LIGHTMAPS		128
#define MAX_SURFACE_LIGHTMAP	255 //maximum allowed size of a surface vanilla limit was 16+1(for linear sampling), although glquake used 18 for some reason. bigger values allow for less surfaces in certain places

#define	GL_UNUSED_TEXTURE	(~(GLuint)0)

#define	gl_solid_format		3
#define	gl_alpha_format		4

/* # of supported texture filter modes[] (gl_draw.c) */
#define	NUM_GL_FILTERS		6

/* defs for palettized textures	*/
#define	INVERSE_PAL_R_BITS	6
#define	INVERSE_PAL_G_BITS	6
#define	INVERSE_PAL_B_BITS	6
#define	INVERSE_PAL_TOTAL_BITS	(INVERSE_PAL_R_BITS + INVERSE_PAL_G_BITS + INVERSE_PAL_B_BITS)
#define	INVERSE_PAL_SIZE	(1 << INVERSE_PAL_TOTAL_BITS)

/* r_local.h defs		*/
#define ALIAS_BASE_SIZE_RATIO		(1.0 / 11.0)
					// normalizing factor so player model works
					// out to about 1 pixel per triangle
#define MAX_SKIN_HEIGHT		480

#define BACKFACE_EPSILON	0.01

#define SKYSHIFT		7
#define	SKYSIZE			(1 << SKYSHIFT)
#define SKYMASK			(SKYSIZE - 1)

extern int gl_warpimagesize; //johnfitz -- for water warp

extern qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz

/* ====================================================================
   ENDIANNESS: RGBA
   ================================================================== */

#if ENDIAN_RUNTIME_DETECT

/* initialized by VID_Init() */
extern unsigned int	MASK_r;
extern unsigned int	MASK_g;
extern unsigned int	MASK_b;
extern unsigned int	MASK_a;
extern unsigned int	MASK_rgb;
extern unsigned int	SHIFT_r;
extern unsigned int	SHIFT_g;
extern unsigned int	SHIFT_b;
extern unsigned int	SHIFT_a;

#else	/* ENDIAN_RUNTIME_DETECT */

#if (BYTE_ORDER == BIG_ENDIAN)	/* R G B A */
#define	MASK_r		0xff000000
#define	MASK_g		0x00ff0000
#define	MASK_b		0x0000ff00
#define	MASK_a		0x000000ff
#define	SHIFT_r		24
#define	SHIFT_g		16
#define	SHIFT_b		8
#define	SHIFT_a		0
#elif (BYTE_ORDER == LITTLE_ENDIAN) /* A B G R */
#define	MASK_r		0x000000ff
#define	MASK_g		0x0000ff00
#define	MASK_b		0x00ff0000
#define	MASK_a		0xff000000
#define	SHIFT_r		0
#define	SHIFT_g		8
#define	SHIFT_b		16
#define	SHIFT_a		24
#endif

#define	MASK_rgb	(MASK_r|MASK_g|MASK_b)

#endif	/* ENDIAN_RUNTIME_DETECT */


/* ====================================================================
   TYPES
   ================================================================== */

/*
typedef struct
{
	GLuint		texnum;
	char	identifier[MAX_QPATH];
	int		width, height;
	int		flags;
	unsigned short	crc;
} gltexture_t;

typedef struct
{
	const char	*name;
	int	minimize, maximize;
} glmode_t;
*/
/* texture filters */
typedef struct
{
	int	magfilter;
	int minfilter;
	char *name;
} glmode_t;
static glmode_t modes[] = {
	{GL_NEAREST, GL_NEAREST,				"GL_NEAREST"},
	{GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST,	"GL_NEAREST_MIPMAP_NEAREST"},
	{GL_NEAREST, GL_NEAREST_MIPMAP_LINEAR,	"GL_NEAREST_MIPMAP_LINEAR"},
	{GL_LINEAR,  GL_LINEAR,					"GL_LINEAR"},
	{GL_LINEAR,  GL_LINEAR_MIPMAP_NEAREST,	"GL_LINEAR_MIPMAP_NEAREST"},
	{GL_LINEAR,  GL_LINEAR_MIPMAP_LINEAR,	"GL_LINEAR_MIPMAP_LINEAR"},
};
#define NUM_GLMODES (int)(sizeof(modes)/sizeof(modes[0]))
static int glmode_idx = NUM_GLMODES - 1; /* trilinear */

//johnfitz -- GL_EXT_texture_env_combine
//the values for GL_ARB_ are identical
#define GL_COMBINE_EXT		0x8570
#define GL_COMBINE_RGB_EXT	0x8571
#define GL_COMBINE_ALPHA_EXT	0x8572
#define GL_RGB_SCALE_EXT	0x8573
#define GL_CONSTANT_EXT		0x8576
#define GL_PRIMARY_COLOR_EXT	0x8577
#define GL_PREVIOUS_EXT		0x8578
#define GL_SOURCE0_RGB_EXT	0x8580
#define GL_SOURCE1_RGB_EXT	0x8581
#define GL_SOURCE0_ALPHA_EXT	0x8588
#define GL_SOURCE1_ALPHA_EXT	0x8589


typedef struct cachepic_s
{
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	/* for appended glpic */
} cachepic_t;

typedef struct
{
	gltexture_t *gltexture;
	float		sl, tl, sh, th;
} glpic_t;

extern int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
extern    qboolean        mtexenabled;
extern int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;
extern    entity_t        *currententity;
extern qboolean gl_texture_env_add; // for GL_EXT_texture_env_add
extern float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha; //ericw

/* particle enums and types: note that hexen2 and
   hexenworld versions of these are different!! */
#include "particle.h"


/* ====================================================================
   GLOBAL VARIABLES
   ================================================================== */

/* gl texture objects */
//extern	GLuint		currenttexture;
static GLuint	currenttexture[3] = { GL_UNUSED_TEXTURE, GL_UNUSED_TEXTURE, GL_UNUSED_TEXTURE }; // to avoid unnecessary texture sets
//extern	GLuint		particletexture;
static gltexture_t *particletexture, *particletexture1, *particletexture2, *particletexture3, *particletexture4; //johnfitz
extern gltexture_t *lightmap_textures[MAX_LIGHTMAPS]; //johnfitz -- changed to an array
//static	GLuint		playertextures[MAX_CLIENTS];
static gltexture_t *playertextures[MAX_CLIENTS]; //johnfitz -- changed to an array of pointers
extern	gltexture_t		*gl_extra_textures[MAX_EXTRA_TEXTURES];	// generic textures for models

//ericw -- VBO
extern PFNGLBINDBUFFERARBPROC  GL_BindBufferFunc;
extern PFNGLBUFFERDATAARBPROC  GL_BufferDataFunc;
extern PFNGLBUFFERSUBDATAARBPROC  GL_BufferSubDataFunc;
extern PFNGLDELETEBUFFERSARBPROC  GL_DeleteBuffersFunc;
extern PFNGLGENBUFFERSARBPROC  GL_GenBuffersFunc;
extern	qboolean	gl_vbo_able;
//ericw


//ericw -- GLSL

// SDL 1.2 has a bug where it doesn't provide these typedefs on OS X!
typedef GLuint(APIENTRYP QS_PFNGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRYP QS_PFNGLDELETESHADERPROC) (GLuint shader);
typedef void (APIENTRYP QS_PFNGLDELETEPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP QS_PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (APIENTRYP QS_PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef void (APIENTRYP QS_PFNGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint *params);
typedef void (APIENTRYP QS_PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRYP QS_PFNGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRYP QS_PFNGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef GLuint(APIENTRYP QS_PFNGLCREATEPROGRAMPROC) (void);
typedef void (APIENTRYP QS_PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRYP QS_PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP QS_PFNGLBINDATTRIBLOCATIONFUNC) (GLuint program, GLuint index, const GLchar *name);
typedef void (APIENTRYP QS_PFNGLUSEPROGRAMPROC) (GLuint program);
typedef GLint(APIENTRYP QS_PFNGLGETATTRIBLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (APIENTRYP QS_PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (APIENTRYP QS_PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRYP QS_PFNGLDISABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef GLint(APIENTRYP QS_PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (APIENTRYP QS_PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (APIENTRYP QS_PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (APIENTRYP QS_PFNGLUNIFORM3FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRYP QS_PFNGLUNIFORM4FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

extern QS_PFNGLCREATESHADERPROC GL_CreateShaderFunc;
extern QS_PFNGLDELETESHADERPROC GL_DeleteShaderFunc;
extern QS_PFNGLDELETEPROGRAMPROC GL_DeleteProgramFunc;
extern QS_PFNGLSHADERSOURCEPROC GL_ShaderSourceFunc;
extern QS_PFNGLCOMPILESHADERPROC GL_CompileShaderFunc;
extern QS_PFNGLGETSHADERIVPROC GL_GetShaderivFunc;
extern QS_PFNGLGETSHADERINFOLOGPROC GL_GetShaderInfoLogFunc;
extern QS_PFNGLGETPROGRAMIVPROC GL_GetProgramivFunc;
extern QS_PFNGLGETPROGRAMINFOLOGPROC GL_GetProgramInfoLogFunc;
extern QS_PFNGLCREATEPROGRAMPROC GL_CreateProgramFunc;
extern QS_PFNGLATTACHSHADERPROC GL_AttachShaderFunc;
extern QS_PFNGLLINKPROGRAMPROC GL_LinkProgramFunc;
extern QS_PFNGLBINDATTRIBLOCATIONFUNC GL_BindAttribLocationFunc;
extern QS_PFNGLUSEPROGRAMPROC GL_UseProgramFunc;
extern QS_PFNGLGETATTRIBLOCATIONPROC GL_GetAttribLocationFunc;
extern QS_PFNGLVERTEXATTRIBPOINTERPROC GL_VertexAttribPointerFunc;
extern QS_PFNGLENABLEVERTEXATTRIBARRAYPROC GL_EnableVertexAttribArrayFunc;
extern QS_PFNGLDISABLEVERTEXATTRIBARRAYPROC GL_DisableVertexAttribArrayFunc;
extern QS_PFNGLGETUNIFORMLOCATIONPROC GL_GetUniformLocationFunc;
extern QS_PFNGLUNIFORM1IPROC GL_Uniform1iFunc;
extern QS_PFNGLUNIFORM1FPROC GL_Uniform1fFunc;
extern QS_PFNGLUNIFORM3FPROC GL_Uniform3fFunc;
extern QS_PFNGLUNIFORM4FPROC GL_Uniform4fFunc;
extern	qboolean	gl_glsl_able;
extern	qboolean	gl_glsl_gamma_able;
extern	qboolean	gl_glsl_alias_able;
// ericw --


// Multitexture
extern	qboolean	mtexenabled;
extern	qboolean	gl_mtexable;
extern PFNGLMULTITEXCOORD2FARBPROC  GL_MTexCoord2fFunc;
extern PFNGLACTIVETEXTUREARBPROC    GL_SelectTextureFunc;
extern PFNGLCLIENTACTIVETEXTUREARBPROC	GL_ClientActiveTextureFunc;
extern GLint		gl_max_texture_units; //ericw
//ericw


/* the GL_Bind macro */
/*
#define GL_Bind(texnum)							\
	do {								\
		if (currenttexture != (texnum))				\
		{							\
			currenttexture = (texnum);			\
			glBindTexture_fp(GL_TEXTURE_2D,currenttexture);	\
		}							\
	} while (0)
*/
extern	int		gl_texlevel;
extern	int		numgltextures;
extern	qboolean	flush_textures;
extern	gltexture_t	gltextures[MAX_GLTEXTURES];
extern qboolean gl_texture_env_combine;

extern	int		gl_filter_idx;
extern	float		gldepthmin, gldepthmax;
extern	int		glx, gly, glwidth, glheight;

/* hardware-caps related globals */
extern	GLfloat		gl_max_anisotropy;
extern	qboolean	gl_tex_NPOT;
extern	qboolean	is_3dfx;
extern	qboolean	is8bit;
extern	qboolean	gl_mtexable;
extern	qboolean	have_stencil;

/* view origin */
extern	vec3_t		vup;
extern	vec3_t		vpn;
extern	vec3_t		vright;
extern	vec3_t		r_origin;

/* screen size info */
extern	refdef_t	r_refdef;
extern	vrect_t		scr_vrect;

extern	mleaf_t		*r_viewleaf, *r_oldviewleaf;
extern	float		r_world_matrix[16];
extern	entity_t	r_worldentity;
extern	qboolean	r_cache_thrash;		// compatability
extern	vec3_t		modelorg, r_entorigin;
extern	int		r_visframecount;	// ??? what difs?
extern	int		r_framecount;
extern	mplane_t	frustum[4];
extern	int		c_brush_polys, c_alias_polys;

/* palette stuff */
extern	const int		ColorIndex[16];
extern	const unsigned int	ColorPercent[16];
extern	float		RTint[256], GTint[256], BTint[256];
extern	unsigned char	*inverse_pal;

/* global cvars */
extern	cvar_t	r_norefresh;
extern	cvar_t	r_drawentities;
extern	cvar_t	r_drawworld;
extern	cvar_t	r_drawviewmodel;
extern	cvar_t	r_speeds;
extern	cvar_t	r_waterwarp;
extern	cvar_t	r_fullbright;
extern	cvar_t	r_lightmap;
extern	cvar_t	r_shadows;
extern	cvar_t	r_mirroralpha;
extern	cvar_t	r_wateralpha;
extern	cvar_t	r_skyalpha;
extern	cvar_t	r_dynamic;
extern	cvar_t	r_novis;
extern	cvar_t	r_wholeframe;
extern	cvar_t	r_texture_external;

#if defined(H2W)
extern	cvar_t	r_netgraph;
extern	cvar_t	r_entdistance;
extern	cvar_t	r_teamcolor;
#endif	/* H2W */

extern	cvar_t	gl_playermip;

extern	cvar_t	gl_clear;
extern	cvar_t	gl_cull;
extern	cvar_t	gl_poly;
extern	cvar_t	gl_ztrick;
extern	cvar_t	gl_zfix;
extern	cvar_t	gl_purge_maptex;
extern	cvar_t	gl_smoothmodels;
extern	cvar_t	gl_affinemodels;
extern	cvar_t	gl_polyblend;
extern	cvar_t	gl_keeptjunctions;
extern	cvar_t	gl_reporttjunctions;
extern	cvar_t	gl_flashblend;
extern	cvar_t	gl_nocolors;
extern	cvar_t	gl_waterripple;
extern	cvar_t	gl_glows;
extern	cvar_t	gl_other_glows;
extern	cvar_t	gl_missile_glows;

extern	cvar_t	gl_coloredlight;
extern	cvar_t	gl_colored_dynamic_lights;
extern	cvar_t	gl_extra_dynamic_lights;
extern	cvar_t	gl_lightmapfmt;
static	cvar_t	gl_max_size;
static	cvar_t	vid_gamma;

/* other globals */
extern	int		gl_coloredstatic;	/* value of gl_coloredlight stored at level start */
extern	int		gl_lightmap_format;	/* value of gl_lightmapfmt stored at level start */

extern	vec3_t		lightcolor;
extern	vec3_t		lightspot;

extern	texture_t	*r_notexture_mip;
extern	int		d_lightstylevalue[256];	// 8.8 fraction of base light value

extern	byte		*playerTranslation;
extern	const int	color_offsets[MAX_PLAYER_CLASS];

extern	qboolean	mirror;
extern	mplane_t	*mirror_plane;
extern	int		mirrortexturenum;	/* quake texturenum, not gltexturenum */
extern	int		skytexturenum;		/* index in cl.loadmodel, not gl texture object */


/* ====================================================================
   GLOBAL FUNCTIONS
   ================================================================== */

void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);

void GL_Set2D (void);

/* LoadTexture Flags */
#define	TEX_DEFAULT		0
#define	TEX_MIPMAP		(1 << 1)
#define	TEX_ALPHA		(1 << 2)
#define	TEX_RGBA		(1 << 5)	/* texture is 32 bit RGBA, not 8 bit */
/* TEX_NEAREST and TEX_LINEAR aren't supposed to be ORed with TEX_MIPMAP */
#define	TEX_NEAREST		(1 << 6)	/* force point sampled */
#define	TEX_LINEAR		(1 << 7)	/* force linear filtering */
/* duplicated EF_ values from gl_model.h: */
#define	TEX_TRANSPARENT		(1 << 12)	/* Transparent sprite				*/
#define	TEX_HOLEY		(1 << 14)	/* Solid model with color 0			*/
#define	TEX_SPECIAL_TRANS	(1 << 15)	/* Translucency through the particle table	*/

gltexture_t* GL_LoadPicTexture (qpic_t *pic);
void D_ClearOpenGLTextures (int last_tex);

qboolean R_CullBox (vec3_t mins, vec3_t maxs);
void DrawGLTriangleFan(glpoly_t *p);
void R_DrawBrushModel (entity_t *e, qboolean Translucent);
void R_DrawWorld (void);
void R_RenderBrushPoly (entity_t *e, msurface_t *fa, qboolean override);
void R_RotateForEntity (entity_t *e);
void DrawWaterPoly(glpoly_t *p);
void DrawGLPoly(glpoly_t *p);
void R_StoreEfrags (efrag_t **ppefrag);
void GL_BindBuffer(GLenum target, GLuint buffer);
void GL_ClearBufferBindings();


#if defined(QUAKE2)
void R_LoadSkys (void);
void R_DrawSkyBox (void);
void R_ClearSkyBox (void);
#endif	/* QUAKE2 */
void GL_SubdivideSurface (msurface_t *fa);
void EmitWaterPolys (msurface_t *fa);
void EmitBothSkyLayers (msurface_t *fa);
void R_DrawSkyChain (msurface_t *s);
//void R_DrawWaterSurfaces (void);

void R_RenderDlights (void);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);
void R_AnimateLight(void);
int R_LightPoint (vec3_t p);
float R_LightPointColor (vec3_t p);
void GL_BuildLightmaps (void);
void GL_SetupLightmapFmt (void);
void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *hdr);
extern void *GLARB_GetXYZOffset(aliashdr_t *hdr, int pose);
void R_RenderDynamicLightmaps(msurface_t *fa);
void R_UploadLightmaps(void);

void R_InitParticleTexture (void);
void R_InitExtraTextures (void);
#if defined(H2W)
void R_NetGraph (void);
void R_InitNetgraphTexture (void);
#endif

void R_ReadPointFile_f (void);
void R_TranslatePlayerSkin (int playernum);
void R_TranslateNewPlayerSkin (int playernum);
qboolean R_CullModelForEntity(entity_t *e);
float GL_WaterAlphaForSurface(msurface_t *fa);

extern	cvar_t	gl_subdivide_size;
extern	float	load_subdivide_size; //johnfitz -- remember what subdivide_size value was when this map was loaded

#endif	/* GLQUAKE_H */

