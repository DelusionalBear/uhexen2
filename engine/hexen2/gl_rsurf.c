/*
 * r_surf.c -- surface-related refresh code
 * $Id$
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 1997-1998  Raven Software Corp.
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

#include "quakedef.h"

int		gl_lightmap_format = GL_RGBA;
cvar_t		gl_lightmapfmt = {"gl_lightmapfmt", "GL_RGBA", CVAR_ARCHIVE};
int		lightmap_bytes = 4;		// 1, 2, or 4. default is 4 for GL_RGBA
gltexture_t        *lightmap_textures[MAX_LIGHTMAPS]; //johnfitz -- changed to an array

static unsigned int	blocklights[MAX_SURFACE_LIGHTMAP*MAX_SURFACE_LIGHTMAP];
static unsigned int	blocklightscolor[MAX_SURFACE_LIGHTMAP*MAX_SURFACE_LIGHTMAP*3];	// colored light support. *3 for RGB to the definitions at the top

#define	BLOCK_WIDTH	256
#define	BLOCK_HEIGHT	256

typedef struct glRect_s {
	unsigned char l, t, w, h;
} glRect_t;

static glpoly_t	*lightmap_polys[MAX_LIGHTMAPS];
static qboolean	lightmap_modified[MAX_LIGHTMAPS];
glRect_t	lightmap_rectchange[MAX_LIGHTMAPS];


static int	allocated[MAX_LIGHTMAPS][BLOCK_WIDTH];

// the lightmap texture data needs to be kept in
// main memory so texsubimage can update properly
static byte	lightmaps[4*MAX_LIGHTMAPS*BLOCK_WIDTH*BLOCK_HEIGHT];


void Sky_DrawFaceQuad(glpoly_t *p);

/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	// vars for lit support
	float		cred, cgreen, cblue, brightness;
	unsigned int	*bl;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	tex = surf->texinfo;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		if ( !(surf->dlightbits & (1<<lnum)) )
			continue;		// not lit by this light

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].origin, surf->plane->normal) - surf->plane->dist;
		rad -= fabs(dist);
		minlight = cl_dlights[lnum].minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i = 0; i < 3; i++)
		{
			impact[i] = cl_dlights[lnum].origin[i] - surf->plane->normal[i]*dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];

		// lit support (LordHavoc)
		bl = blocklightscolor;
		cred = cl_dlights[lnum].color[0] * 256.0f;
		cgreen = cl_dlights[lnum].color[1] * 256.0f;
		cblue = cl_dlights[lnum].color[2] * 256.0f;

		for (t = 0; t < tmax; t++)
		{
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			for (s = 0; s < smax; s++)
			{
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);
				if (dist < minlight)
				{
					brightness = rad - dist;
					if (cl_dlights[lnum].dark)
					{
						// clamp to 0
						bl[0] -= (int)(((brightness * cred) < bl[0]) ? (brightness * cred) : bl[0]);
						bl[1] -= (int)(((brightness * cgreen) < bl[1]) ? (brightness * cgreen) : bl[1]);
						bl[2] -= (int)(((brightness * cblue) < bl[2]) ? (brightness * cblue) : bl[2]);
					}
					else
					{
						bl[0] += (int)(brightness * cred);
						bl[1] += (int)(brightness * cgreen);
						bl[2] += (int)(brightness * cblue);
					}

					blocklights[t*smax + s] += (rad - dist)*256;
				}

				bl += 3;
			}
		}
	}
}


/*
===============
GL_SetupLightmapFmt

Used to setup the lightmap_format and lightmap_bytes
during init from VID_Init() and at every level change
from Mod_LoadLighting().
===============
*/
void GL_SetupLightmapFmt (void)
{
	// only GL_LUMINANCE and GL_RGBA are supported
	if (!q_strcasecmp(gl_lightmapfmt.string, "GL_LUMINANCE"))
		gl_lightmap_format = GL_LUMINANCE;
	else if (!q_strcasecmp(gl_lightmapfmt.string, "GL_RGBA"))
		gl_lightmap_format = GL_RGBA;
	else
	{
		gl_lightmap_format = GL_RGBA;
		Cvar_SetQuick (&gl_lightmapfmt, "GL_RGBA");
	}

	if (!host_initialized) // check for cmdline overrides
	{
		if (COM_CheckParm ("-lm_1"))
		{
			gl_lightmap_format = GL_LUMINANCE;
			Cvar_SetQuick (&gl_lightmapfmt, "GL_LUMINANCE");
		}
		else if (COM_CheckParm ("-lm_4"))
		{
			gl_lightmap_format = GL_RGBA;
			Cvar_SetQuick (&gl_lightmapfmt, "GL_RGBA");
		}
	}

	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		lightmap_bytes = 4;
		break;
	case GL_LUMINANCE:
		lightmap_bytes = 1;
		break;
	}
}

/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
static void R_BuildLightMap(msurface_t *surf, byte *dest, int stride)
{
	int		smax, tmax;
	int		t, r, s, q;
	int		i, j, size;
	byte		*lightmap;
	unsigned int	scale;
	int		maps;
	unsigned int	*bl, *blcr, *blcg, *blcb;

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	size = smax * tmax;
	lightmap = surf->samples;

	// set to full bright if no light data
	if (r_fullbright.integer || !cl.worldmodel->lightdata)
	{
		for (i = 0; i < size; i++)
		{
			if (gl_lightmap_format == GL_RGBA)
				blocklightscolor[i * 3 + 0] =
				blocklightscolor[i * 3 + 1] =
				blocklightscolor[i * 3 + 2] = 65280;
			else
				blocklights[i] = 255 * 256;
		}
		goto store;
	}

	// clear to no light
	memset(&blocklightscolor[0], 0, size * 3 * sizeof(unsigned int)); //johnfitz -- lit support via lordhavoc

	for (i = 0; i < size; i++)
	{
		if (gl_lightmap_format == GL_RGBA)
			blocklightscolor[i * 3 + 0] =
			blocklightscolor[i * 3 + 1] =
			blocklightscolor[i * 3 + 2] = 0;
		else
			blocklights[i] = 0;
	}

	// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;	// 8.8 fraction

			if (gl_lightmap_format == GL_RGBA)
			{
				//johnfitz -- lit support via lordhavoc
				bl = blocklightscolor;
				for (i = 0; i < size; i++)
				{
					*bl++ += *lightmap++ * scale;
					*bl++ += *lightmap++ * scale;
					*bl++ += *lightmap++ * scale;
				}
				//johnfitz
			}
			else
			{
				//johnfitz -- lit support via lordhavoc
				bl = blocklights;
				for (i = 0; i < size; i++)
					*bl++ += *lightmap++ * scale;
				//johnfitz
			}
		}
	}

	// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights(surf);

	// bound, invert, and shift
store:
	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		stride -= smax * 4;
		bl = blocklightscolor;
		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				blcr = q = *bl++ >> 7;
				blcg = r = *bl++ >> 7;
				blcb = s = *bl++ >> 7;

				if (!gl_coloredlight.value)
				{
					t = (int)((float)q * 0.33f + (float)s * 0.33f + (float)r * 0.33f);
					if (t > 255)
						t = 255;
					blcr = t;
					blcg = t;
					blcb = t;
				}

				*dest++ = (blcr > 255) ? 255 : blcr;
				*dest++ = (blcg > 255) ? 255 : blcg;
				*dest++ = (blcb > 255) ? 255 : blcb;
				*dest++ = 255;
			}
		}
		break;

	case GL_LUMINANCE:
		bl = blocklights;
		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				t = *bl++;
				t >>= 7;
				if (t > 255)
					t = 255;
				dest[j] = 255 - t;
			}
		}
		break;
	default:
		Sys_Error("Bad lightmap format");
	}
}

/*
===============
R_UploadLightmap -- johnfitz -- uploads the modified lightmap to opengl if necessary

assumes lightmap texture is already bound
===============
*/
static void R_UploadLightmap(int lmap)
{
	glRect_t	*theRect;

	if (!lightmap_modified[lmap])
		return;

	lightmap_modified[lmap] = false;

	theRect = &lightmap_rectchange[lmap];
	glTexSubImage2D_fp(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH, theRect->h, gl_lightmap_format,
		GL_UNSIGNED_BYTE, lightmaps + (lmap* BLOCK_HEIGHT + theRect->t) *BLOCK_WIDTH*lightmap_bytes);
	theRect->l = BLOCK_WIDTH;
	theRect->t = BLOCK_HEIGHT;
	theRect->h = 0;
	theRect->w = 0;
}

void R_UploadLightmaps(void)
{
	int lmap;

	if (gl_mtexable)
		GL_EnableMultitexture();

	for (lmap = 0; lmap < MAX_LIGHTMAPS; lmap++)
	{
		if (!lightmap_modified[lmap])
			continue;

		GL_Bind(lightmap_textures[lmap]);
		R_UploadLightmap(lmap);
	}

	if (gl_mtexable)
		GL_DisableMultitexture();
}

/*
================
R_RenderDynamicLightmaps
called during rendering
================
*/
void R_RenderDynamicLightmaps(msurface_t *fa)
{
	byte		*base;
	int		smax, tmax;
	int		maps;
	glRect_t    *theRect;


	if (fa->flags & SURF_DRAWTILED) //johnfitz -- not a lightmapped surface
		return;

	// add to lightmap chain
	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;

	if (fa->dlightframe == r_framecount	// dynamic this frame
		|| fa->cached_dlight)			// dynamic previously
	{
	dynamic:
		if (r_dynamic.integer)
		{
			lightmap_modified[fa->lightmaptexturenum] = true;
			theRect = &lightmap_rectchange[fa->lightmaptexturenum];
			if (fa->light_t < theRect->t) {
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l) {
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			smax = (fa->extents[0] >> 4) + 1;
			tmax = (fa->extents[1] >> 4) + 1;
			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s - theRect->l) + smax;
			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t - theRect->t) + tmax;
			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap(fa, base, BLOCK_WIDTH*lightmap_bytes);
		}
	}
}

/*
================
R_BuildLightmapChains -- johnfitz -- used for r_lightmap 1

ericw -- now always used at the start of R_DrawTextureChains for the
mh dynamic lighting speedup
================
*/
static void R_BuildLightmapChains(qmodel_t *mod, texchain_t chain)
{
	texture_t *t;
	msurface_t *s;
	int i;

	// clear lightmap chains (already done in r_marksurfaces, but clearing them here to be safe becuase of r_stereo)
	memset(lightmap_polys, 0, sizeof(lightmap_polys));

	// now rebuild them
	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
			continue;

		s = t->texturechains[chain_world];

		for (s = t->texturechains[chain]; s; s = s->texturechain)
			if (!s->culled)
				R_RenderDynamicLightmaps(s);
	}
}

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
static texture_t *R_TextureAnimation (entity_t *e, texture_t *base)
{
	int		reletive;
	int		count;

	if (e->frame)
	{
		if (base->alternate_anims)
			base = base->alternate_anims;
	}

	if (!base->anim_total)
		return base;

	reletive = (int)(cl.time*10) % base->anim_total;

	count = 0;
	while (base->anim_min > reletive || base->anim_max <= reletive)
	{
		base = base->anim_next;
		if (!base)
			Sys_Error ("%s: broken cycle", __thisfunc__);
		if (++count > 100)
			Sys_Error ("%s: infinite cycle", __thisfunc__);
	}

	return base;
}


/*
=============================================================

BRUSH MODELS

=============================================================
*/

/*
================
DrawGLWaterPoly

Warp the vertex coordinates
================
*/
static void DrawGLWaterPoly (glpoly_t *p)
{
	int	i;
	float	*v;
	vec3_t	nv;

	glBegin_fp (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f_fp (v[3], v[4]);

		if (r_waterwarp.integer)
		{
			nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*sin(v[2]*0.05+realtime);
			nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*sin(v[2]*0.05+realtime);
			nv[2] = v[2];
			glVertex3fv_fp (nv);
		}
		else
		{
			glVertex3fv_fp (v);
		}
	}
	glEnd_fp ();
}

static void DrawGLWaterPolyLightmap (glpoly_t *p)
{
	int	i;
	float	*v;
	vec3_t	nv;

	glBegin_fp (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f_fp (v[5], v[6]);

		if (r_waterwarp.integer)
		{
			nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*sin(v[2]*0.05+realtime);
			nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*sin(v[2]*0.05+realtime);
			nv[2] = v[2];
			glVertex3fv_fp (nv);
		}
		else
		{
			glVertex3fv_fp (v);
		}
	}
	glEnd_fp ();
}

static void DrawGLWaterPolyMTexLM (glpoly_t *p)
{
	int	i;
	float	*v;
	vec3_t	nv;

	glBegin_fp (GL_TRIANGLE_FAN);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v+= VERTEXSIZE)
	{
		glMultiTexCoord2fARB_fp (GL_TEXTURE0_ARB, v[3], v[4]);
		glMultiTexCoord2fARB_fp (GL_TEXTURE1_ARB, v[5], v[6]);

		if (r_waterwarp.integer)
		{
			nv[0] = v[0] + 8*sin(v[1]*0.05+realtime)*sin(v[2]*0.05+realtime);
			nv[1] = v[1] + 8*sin(v[0]*0.05+realtime)*sin(v[2]*0.05+realtime);
			nv[2] = v[2];
			glVertex3fv_fp (nv);
		}
		else
		{
			glVertex3fv_fp (v);
		}
	}
	glEnd_fp ();
}

/*
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p)
{
	int	i;
	float	*v;

	glBegin_fp (GL_POLYGON);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v+= VERTEXSIZE)
	{
		glTexCoord2f_fp (v[3], v[4]);
		glVertex3fv_fp (v);
	}
	glEnd_fp ();
}

void DrawGLPolyMTex (glpoly_t *p)
{
	int	i;
	float	*v;

	glBegin_fp (GL_POLYGON);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v+= VERTEXSIZE)
	{
		glMultiTexCoord2fARB_fp (GL_TEXTURE0_ARB, v[3], v[4]);
		glMultiTexCoord2fARB_fp (GL_TEXTURE1_ARB, v[5], v[6]);

		glVertex3fv_fp (v);
	}
	glEnd_fp ();
}

/*
================
R_DrawLightmapChains -- Shanjaq -- R_BlendLightmaps stripped down to almost nothing
================
*/
static void R_DrawLightmapChains() 
{
	unsigned int		i;
	int			j;
	glpoly_t	*p;
	float		*v;

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!lightmap_polys[i])
			continue;	// skip if no lightmap

		GL_Bind(lightmap_textures[i]);
		for (p = lightmap_polys[i]; p; p = p->chain)
		{
			if (p->flags & SURF_UNDERWATER)
			{
				DrawGLWaterPolyLightmap(p);
			}
			else
			{
				glBegin_fp(GL_POLYGON);
				v = p->verts[0];
				for (j = 0; j < p->numverts; j++, v += VERTEXSIZE)
				{
					glTexCoord2f_fp(v[5], v[6]);
					glVertex3fv_fp(v);
				}
				glEnd_fp();
			}
		}
	}
}

/*
================
R_BlendLightmaps
================
*/
static void R_BlendLightmaps (qboolean Translucent)
{
	unsigned int		i;
	int			j;
	glpoly_t	*p;
	float		*v;

	if (r_fullbright.integer)
		return;

	if (!Translucent)
		glDepthMask_fp (0);	// don't bother writing Z

	if (gl_lightmap_format == GL_RGBA)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		//glColor4f_fp (1.0f,1.0f,0.0f, 1.0f);
		glBlendFunc_fp (GL_ZERO, GL_SRC_COLOR);
	}
	else if (gl_lightmap_format == GL_LUMINANCE)
	{
		glBlendFunc_fp (GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
	}

	if (!r_lightmap.integer)
	{
		glEnable_fp (GL_BLEND);
	}

	if (! lightmap_textures[0])
	{
		// if lightmaps were hosed in a video mode change, make
		// sure we allocate new slots for lightmaps, otherwise
		// we'll probably overwrite some other existing textures.
		//glGenTextures_fp(MAX_LIGHTMAPS, lightmap_textures);
	}

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		p = lightmap_polys[i];
		if (!p)
			continue;	// skip if no lightmap

		GL_Bind(lightmap_textures[i]);

		if (lightmap_modified[i])
		{
			// if current lightmap was changed reload it
			// and mark as not changed.
			lightmap_modified[i] = false;
			glTexImage2D_fp (GL_TEXTURE_2D, 0, lightmap_bytes, BLOCK_WIDTH,
					BLOCK_HEIGHT, 0, gl_lightmap_format, GL_UNSIGNED_BYTE,
					lightmaps + i*BLOCK_WIDTH*BLOCK_HEIGHT*lightmap_bytes);
		}

		for ( ; p ; p = p->chain)
		{
			if (p->flags & SURF_UNDERWATER)
				DrawGLWaterPolyLightmap (p);
			else
			{
				glBegin_fp (GL_POLYGON);
				v = p->verts[0];
				for (j = 0; j < p->numverts; j++, v+= VERTEXSIZE)
				{
					glTexCoord2f_fp (v[5], v[6]);
					glVertex3fv_fp (v);
				}
				glEnd_fp ();
			}
		}
	}

	if (!r_lightmap.integer)
	{
		glDisable_fp (GL_BLEND);
	}

	if (gl_lightmap_format == GL_RGBA)
	{
		glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}
	else if (gl_lightmap_format == GL_LUMINANCE)
	{
		glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	if (!Translucent)
		glDepthMask_fp (1);	// back to normal Z buffering
}

static void R_UpdateLightmaps (qboolean Translucent)
{
	unsigned int		i;
	glpoly_t	*p;

	if (r_fullbright.integer)
		return;

	//glActiveTextureARB_fp (GL_TEXTURE1_ARB);
	GL_EnableMultitexture();

	if (! lightmap_textures[0])
	{
		// if lightmaps were hosed in a video mode change, make
		// sure we allocate new slots for lightmaps, otherwise
		// we'll probably overwrite some other existing textures.
		//glGenTextures_fp(MAX_LIGHTMAPS, lightmap_textures);
	}

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		p = lightmap_polys[i];
		if (!p)
			continue;	// skip if no lightmap

		GL_Bind(lightmap_textures[i]);

		if (lightmap_modified[i])
		{
			// if current lightmap was changed reload it
			// and mark as not changed.
			lightmap_modified[i] = false;
			glTexImage2D_fp (GL_TEXTURE_2D, 0, lightmap_bytes, BLOCK_WIDTH,
					BLOCK_HEIGHT, 0, gl_lightmap_format, GL_UNSIGNED_BYTE,
					lightmaps + i*BLOCK_WIDTH*BLOCK_HEIGHT*lightmap_bytes);
		}
	}

	//glActiveTextureARB_fp (GL_TEXTURE0_ARB);
	GL_DisableMultitexture();
}


/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (entity_t *e, msurface_t *fa, qboolean override, qboolean unlit)
{
	texture_t	*t;
	byte		*base;
	int		maps;
	float		intensity, alpha_val;

	c_brush_polys++;

	if (gl_mtexable)
		glActiveTextureARB_fp(GL_TEXTURE0_ARB);

	intensity = 1.0f;
	alpha_val = 1.0f;

	if (e->drawflags & DRF_TRANSLUCENT)
	{
		glEnable_fp (GL_BLEND);
		alpha_val = r_wateralpha.value;
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}
	if ((e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
	{
		// ent->abslight   0 - 255
		//shan revisit abslight in fog
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glBlendFunc_fp(GL_ONE, GL_ONE);
		intensity = (float)e->abslight / 255.0f;
	}
	else
	{
		////glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		//glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE); //shan this may cause problems...

	}

	if (!override)
	{
		if (unlit)
			glColor4f_fp(0, 0, 0, alpha_val);
		else
			glColor4f_fp(intensity, intensity, intensity, alpha_val);
	}

	if (fa->flags & SURF_DRAWSKY)
	{	// warp texture, no lightmaps
		//EmitBothSkyLayers (fa); //shan
		return;
	}

	t = R_TextureAnimation (e, fa->texinfo->texture);
	GL_Bind (t->gltexture);

	if (fa->flags & SURF_DRAWTURB)
	{	// warp texture, no lightmaps
		EmitWaterPolys (fa);
		return;
	}

	if (fa->flags & SURF_DRAWFENCE)
	{
		glAlphaFunc_fp(GL_GREATER, 0.0);
		glEnable_fp(GL_ALPHA_TEST); // Flip on alpha test
		//glDepthMask_fp(1);
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		//glColor4f_fp(128.0f, 0.0f, 240.0f, 0.7f); //shan test color tint?
	}


	if (gl_mtexable)
	{
		if ((e->drawflags & DRF_TRANSLUCENT) ||
		    (e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
		{
			if (fa->flags & SURF_UNDERWATER)
				DrawGLWaterPoly (fa->polys);
			else
				DrawGLPoly (fa->polys);
		}
		else
		{
			//glActiveTextureARB_fp(GL_TEXTURE1_ARB);
			GL_EnableMultitexture();

			if (gl_lightmap_format == GL_LUMINANCE)
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
			else
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			//glEnable_fp(GL_TEXTURE_2D);
			//glDepthFunc_fp(GL_EQUAL);
			//glDepthMask_fp(0);
			Fog_StartAdditive();

			GL_Bind(lightmap_textures[fa->lightmaptexturenum]);

			Fog_StopAdditive();
			//glDepthMask_fp(1);
			//glDepthFunc_fp(GL_LEQUAL);
			//glEnable_fp (GL_BLEND);

			if (fa->flags & SURF_UNDERWATER)
				DrawGLWaterPolyMTexLM (fa->polys);
			else
				DrawGLPolyMTex (fa->polys);

			//glDisable_fp(GL_TEXTURE_2D);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			//glDisable_fp (GL_BLEND);
			GL_DisableMultitexture();

			//glActiveTextureARB_fp(GL_TEXTURE0_ARB);
		}
	}
	else
	{
		if (fa->flags & SURF_UNDERWATER)
			DrawGLWaterPoly (fa->polys);
		else
			DrawGLPoly (fa->polys);
	}

	if (fa->flags & SURF_DRAWFENCE)
	{
		glDisable_fp(GL_ALPHA_TEST); // Flip alpha test back off
		glAlphaFunc_fp(GL_GREATER, 0.632);
		//glDepthMask_fp(0);
	}

// add the poly to the proper lightmap chain
	fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
	lightmap_polys[fa->lightmaptexturenum] = fa->polys;

	// check for lightmap modification
	for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
	{
		if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
			goto dynamic;
	}

	if (fa->dlightframe == r_framecount	// dynamic this frame
		|| fa->cached_dlight)		// dynamic previously
	{
dynamic:
		if (r_dynamic.integer)
		{
			lightmap_modified[fa->lightmaptexturenum] = true;
			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap (fa, base, BLOCK_WIDTH*lightmap_bytes);
		}
	}

	if ((e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT ||
	    (e->drawflags & DRF_TRANSLUCENT))
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}

	if (e->drawflags & DRF_TRANSLUCENT)
	{
		glDisable_fp (GL_BLEND);
		glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
}

void R_RenderBrushPolyMTex (entity_t *e, msurface_t *fa, qboolean override)
{
	texture_t	*t;
	byte		*base;
	int		maps;
	float		intensity, alpha_val;

	c_brush_polys++;

	glActiveTextureARB_fp(GL_TEXTURE0_ARB);

	intensity = 1.0f;
	alpha_val = 1.0f;

	if (e->drawflags & DRF_TRANSLUCENT)
	{
		glEnable_fp (GL_BLEND);
		alpha_val = r_wateralpha.value;
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	}
	else
	{
		/* KIERO: Seems it's enabled when we enter here. */
		glDisable_fp (GL_BLEND);
	}

	if ((e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		intensity = (float)e->abslight / 255.0f;
	}

	if (fa->flags & SURF_DRAWTURB)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glDisable_fp (GL_BLEND);
		glActiveTextureARB_fp(GL_TEXTURE1_ARB);
		glDisable_fp(GL_TEXTURE_2D);
		glActiveTextureARB_fp(GL_TEXTURE0_ARB);

		intensity = 1.0;
	}

	if (!override)
		glColor4f_fp(intensity, intensity, intensity, alpha_val);

	if (fa->flags & SURF_DRAWSKY)
	{	// warp texture, no lightmaps
		//EmitBothSkyLayers (fa);
		return;
	}

	glActiveTextureARB_fp(GL_TEXTURE0_ARB);
	t = R_TextureAnimation (e, fa->texinfo->texture);
	GL_Bind (t->gltexture);

	if (fa->flags & SURF_DRAWTURB)
	{
		glColor4f_fp(1.0f, 1.0f, 1.0f, 1.0f);
		EmitWaterPolys (fa);
		//return;
	}
	else
	{
		if ((e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
		{
			glActiveTextureARB_fp(GL_TEXTURE0_ARB);

			if (fa->flags & SURF_UNDERWATER)
				DrawGLWaterPoly (fa->polys);
			else
				DrawGLPoly (fa->polys);
		}
		else
		{
			glActiveTextureARB_fp(GL_TEXTURE1_ARB);
			GL_Bind (lightmap_textures[fa->lightmaptexturenum]);

			if (fa->flags & SURF_UNDERWATER)
				DrawGLWaterPolyMTexLM (fa->polys);
			else
				DrawGLPolyMTex (fa->polys);
		}

		glActiveTextureARB_fp(GL_TEXTURE1_ARB);

		// add the poly to the proper lightmap chain
		fa->polys->chain = lightmap_polys[fa->lightmaptexturenum];
		lightmap_polys[fa->lightmaptexturenum] = fa->polys;

		// check for lightmap modification
		for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
		{
			if (d_lightstylevalue[fa->styles[maps]] != fa->cached_light[maps])
				goto dynamic1;
		}

		if (fa->dlightframe == r_framecount	// dynamic this frame
		    || fa->cached_dlight)		// dynamic previously
		{
dynamic1:
			if (r_dynamic.integer)
			{
				lightmap_modified[fa->lightmaptexturenum] = true;
				base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
				base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
				R_BuildLightMap (fa, base, BLOCK_WIDTH*lightmap_bytes);
			}
		}
	}

	glActiveTextureARB_fp(GL_TEXTURE0_ARB);

	if ((e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT ||
	    (e->drawflags & DRF_TRANSLUCENT))
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}

	if (e->drawflags & DRF_TRANSLUCENT)
	{
		glDisable_fp (GL_BLEND);
	}

	glActiveTextureARB_fp(GL_TEXTURE1_ARB);
}


/*
================
R_MirrorChain
================
*/
static void R_MirrorChain (msurface_t *s)
{
	if (mirror)
		return;
	mirror = true;
	mirror_plane = s->plane;
}


/*
================
R_DrawWaterSurfaces
================
*/
void R_DrawWaterSurfaces (void)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;

	if (r_wateralpha.value > 1)
		r_wateralpha.value = 1;
	if (r_wateralpha.value == 1.0)
		return;

	glDepthMask_fp(0);

	//
	// go back to the world matrix
	//
	glLoadMatrixf_fp (r_world_matrix);

	glEnable_fp (GL_BLEND);
	glColor4f_fp (1,1,1,r_wateralpha.value);
	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];
		if (!t)
			continue;
		s = t->texturechains[chain_world];
		if (!s)
			continue;
		if (!(s->flags & SURF_DRAWTURB))
			continue;

		//if (s->flags & SURF_TRANSLUCENT)
		if ((s->flags & SURF_DRAWTURB) && (s->flags & SURF_TRANSLUCENT))
		{
			glColor4f_fp(1, 1, 1, r_wateralpha.value);

			// set modulate mode explicitly
			GL_Bind(t->gltexture);

			//one modulate pass with black fog
			//glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			Fog_StartAdditive();
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE);
			for (; s; s = s->texturechain)
				EmitWaterPolys(s);
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			Fog_StopAdditive();
			//glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		}
		else
		{
			glColor4f_fp(1, 1, 1, 1);

			// set modulate mode explicitly
			GL_Bind(t->gltexture);

			//one pass with no fog
			//Fog_DisableGFog();
			//glEnable_fp(GL_BLEND);
			//glBlendFunc_fp(GL_ONE, GL_ONE);
			//glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			for (; s; s = s->texturechain)
				EmitWaterPolys(s);
			//glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			//glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			//glDisable_fp(GL_BLEND);
			//Fog_EnableGFog();

			//one modulate pass with black fog
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glEnable_fp(GL_BLEND);
			glBlendFunc_fp(GL_ONE, GL_ONE);
			Fog_StartAdditive();
			//for (; s; s = s->texturechain)
			//	EmitWaterPolys(s);
			Fog_StopAdditive();
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable_fp(GL_BLEND);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

			//one additive pass with black geometry and normal fog
			glEnable_fp(GL_BLEND);
			glBlendFunc_fp(GL_ONE, GL_ONE);
			glDepthMask_fp(GL_FALSE);
			glDisable_fp(GL_TEXTURE_2D);
			glColor4f_fp(0, 0, 0, 0);
			//for (; s; s = s->texturechain)
			//	EmitWaterPolys(s);
			glEnable_fp(GL_TEXTURE_2D);
			glDepthMask_fp(GL_TRUE);
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable_fp(GL_BLEND);

		}

		//for ( ; s ; s = s->texturechain)
		//	EmitWaterPolys (s);

		t->texturechains[0] = NULL;
	}

	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	glColor4f_fp (1,1,1,1);
	glDisable_fp (GL_BLEND);
	glDepthMask_fp (1);
}

/*
================
DrawTextureChains_NoTexture -- Shanjaq
================
*/
static void DrawTextureChains_NoTexture(entity_t *e)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
			continue;

		s = t->texturechains[chain_world];

		if (!s)
			continue;

		if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain(s);
			continue;
		}
		else
		{
			qboolean drawFence = false;

			if (s->flags & SURF_DRAWFENCE)
			{
				drawFence = true;
				glEnable_fp(GL_ALPHA_TEST); // Flip on alpha test
			}

			//GL_Bind(R_TextureAnimation(e, s->texinfo->texture)->gltexture);
			glDisable_fp(GL_TEXTURE_2D);

			for (; s; s = s->texturechain)
			{
				if (s->flags & SURF_UNDERWATER)
					DrawGLWaterPoly(s->polys);
				else
					DrawGLPoly(s->polys);

				//DrawGLPoly(s->polys);
			}

			glEnable_fp(GL_TEXTURE_2D);

			if (drawFence)
				glDisable_fp(GL_ALPHA_TEST); // Flip alpha test back off

		}

		t->texturechains[chain_world] = NULL; // shan what is this for?
	}
}

/*
================
DrawTextureChains_Multitexture -- Shanjaq
================
*/
static void DrawTextureChains_Multitexture(entity_t *e)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world] || t->texturechains[chain_world]->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
			continue;

		s = t->texturechains[chain_world];

		if (!s)
			continue;

		if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain(s);
			continue;
		}
		else
		{
			qboolean drawFence = false;

			if (s->flags & SURF_DRAWFENCE)
			{
				drawFence = true;
				glEnable_fp(GL_ALPHA_TEST); // Flip on alpha test
			}

			glActiveTextureARB_fp(GL_TEXTURE0_ARB);
			glEnable_fp(GL_TEXTURE_2D);
			//glActiveTextureARB_fp(GL_TEXTURE1_ARB);
			//glEnable_fp(GL_TEXTURE_2D);
			GL_EnableMultitexture();

			if (gl_lightmap_format == GL_LUMINANCE)
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
			else
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			glEnable_fp(GL_BLEND);

			for (; s; s = s->texturechain)
				R_RenderBrushPolyMTex(e, s, false);

			//glDisable_fp(GL_TEXTURE_2D);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glDisable_fp(GL_BLEND);
			//glActiveTextureARB_fp(GL_TEXTURE0_ARB);

			GL_DisableMultitexture(); // selects TEXTURE0

			if (drawFence)
				glDisable_fp(GL_ALPHA_TEST); // Flip alpha test back off
		}

		t->texturechains[chain_world] = NULL; // shan what is this for?
	}
}


/*
================
DrawTextureChains_TextureOnly -- Shanjaq
================
*/
static void DrawTextureChains_TextureOnly(entity_t *e)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world])
			continue;

		s = t->texturechains[chain_world];

		if (!s)
			continue;

		if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain(s);
			continue;
		}
		else
		{
			qboolean drawFence = false;

			if ((s->flags & (SURF_DRAWTURB | SURF_DRAWSKY)) && r_wateralpha.value != 1.0)
				continue;	// draw translucent water later

			if (s->flags & SURF_DRAWFENCE)
			{
				drawFence = true;
				glEnable_fp(GL_ALPHA_TEST); // Flip on alpha test
			}


			GL_Bind(R_TextureAnimation(e, s->texinfo->texture)->gltexture);

			for (; s; s = s->texturechain)
			{
				if (s->flags & SURF_UNDERWATER)
					DrawGLWaterPoly(s->polys);
				else
					DrawGLPoly(s->polys);
			}

			if (drawFence)
				glDisable_fp(GL_ALPHA_TEST); // Flip alpha test back off

		}

		if (Fog_GetDensity() <= 0.00)
			t->texturechains[chain_world] = NULL; // shan what is this for?
	}
}

/*
================
DrawTextureChains
================
*/
static void DrawTextureChains_New(entity_t *e)
{
	R_BuildLightmapChains(cl.worldmodel, chain_world);
	R_UploadLightmaps();

	if (gl_mtexable) //case 4: texture and lightmap in one pass, regular modulation
	{
		GL_EnableMultitexture();
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		GL_DisableMultitexture();
		if (r_fullbright.integer)
			DrawTextureChains_TextureOnly(e);
		else
			DrawTextureChains_Multitexture(e);
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}
	else
	{
		//case 6: texture in one pass, lightmap in a second pass, fog in third pass

		//to make fog work with multipass lightmapping, need to do one pass
		//with no fog, one modulate pass with black fog, and one additive
		//pass with black geometry and normal fog
		Fog_DisableGFog();
		if (!r_lightmap.integer)
			DrawTextureChains_TextureOnly(e);
		else
			DrawTextureChains_NoTexture(e);
		Fog_EnableGFog();

		glDepthMask_fp(GL_FALSE);
		glEnable_fp(GL_BLEND);
		glBlendFunc_fp(GL_ZERO, GL_SRC_COLOR); //modulate

		if (!r_fullbright.integer)
		{
			//Fog_StartAdditive();

			if (gl_lightmap_format == GL_LUMINANCE)
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
			else
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			//glEnable_fp(GL_BLEND);

			glDepthFunc_fp(GL_EQUAL);
			//glDepthMask_fp(0);
			Fog_StartAdditive();

			R_DrawLightmapChains();

			Fog_StopAdditive();
			//glDepthMask_fp(1);
			glDepthFunc_fp(GL_LEQUAL);

			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			//glDisable_fp(GL_BLEND);
			//Fog_StopAdditive();
		}
		
		if (Fog_GetDensity() > 0.00)
		{
			glDepthFunc_fp(GL_EQUAL);
			glEnable_fp(GL_BLEND);
			glBlendFunc_fp(GL_ONE, GL_ONE); //add
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f_fp(0, 0, 0);
			DrawTextureChains_NoTexture(e);
			glColor3f_fp(1, 1, 1);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glDisable_fp(GL_BLEND);
			glDepthFunc_fp(GL_LEQUAL);
		}
		
		glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable_fp(GL_BLEND);
		glDepthMask_fp(GL_TRUE);
	}
}

/*
================
DrawTextureChains
================
*/
static void DrawTextureChains (entity_t *e)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];
		if (!t)
			continue;
		s = t->texturechains[chain_world];
		if (!s)
			continue;
		//if (i == skytexturenum)
		//	R_DrawSkyChain (s);
		//else
		if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain (s);
			continue;
		}
		else
		{
			if ((s->flags & SURF_DRAWTURB) && r_wateralpha.value != 1.0)
				continue;	// draw translucent water later

			qboolean drawFence = false;

			if (s->flags & SURF_DRAWFENCE)
			{
				drawFence = true;
				glEnable_fp(GL_ALPHA_TEST); // Flip on alpha test
			}

			if (((e->drawflags & DRF_TRANSLUCENT) ||
				(e->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT))
			{
				for ( ; s ; s = s->texturechain)
					R_RenderBrushPoly (e, s, false, false);
			}
			else if (gl_mtexable)
			{
				glActiveTextureARB_fp(GL_TEXTURE0_ARB);
				glEnable_fp(GL_TEXTURE_2D);
				//glActiveTextureARB_fp(GL_TEXTURE1_ARB);
				//glEnable_fp(GL_TEXTURE_2D);
				GL_EnableMultitexture();

				if (gl_lightmap_format == GL_LUMINANCE)
					glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
				else
					glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				glEnable_fp (GL_BLEND);

				for ( ; s ; s = s->texturechain)
					R_RenderBrushPolyMTex (e, s, false);

				//glDisable_fp(GL_TEXTURE_2D);
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glDisable_fp (GL_BLEND);
				//glActiveTextureARB_fp(GL_TEXTURE0_ARB);
				GL_DisableMultitexture();
			}
			else
			{
				for ( ; s ; s = s->texturechain)
					R_RenderBrushPoly (e, s, false, false);
			}
			if (drawFence)
				glDisable_fp(GL_ALPHA_TEST); // Flip alpha test back off

		}

		t->texturechains[chain_world] = NULL;
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e, qboolean Translucent, qboolean unlit)
{
	int		i, k;
	vec3_t		mins, maxs;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	qmodel_t	*clmodel;
	qboolean	rotated;

	currenttexture[0] = GL_UNUSED_TEXTURE;

	clmodel = e->model;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i = 0; i < 3; i++)
		{
			mins[i] = e->origin[i] - clmodel->radius;
			maxs[i] = e->origin[i] + clmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd (e->origin, clmodel->mins, mins);
		VectorAdd (e->origin, clmodel->maxs, maxs);
	}

	if (R_CullBox (mins, maxs))
		return;

#if 0 /* causes side effects in 16 bpp. alternative down below */
	/* Get rid of Z-fighting for textures by offsetting the
	 * drawing of entity models compared to normal polygons.
	 * (Only works if gl_ztrick is turned off) */
	if (gl_zfix.integer && !gl_ztrick.integer)
	{
		glEnable_fp(GL_POLYGON_OFFSET_FILL);
		glEnable_fp(GL_POLYGON_OFFSET_LINE);
	}
#endif /* #if 0 */

	if (!unlit)
		glColor3f_fp (1,1,1);

	memset (lightmap_polys, 0, sizeof(lightmap_polys));

	VectorSubtract (r_refdef.vieworg, e->origin, modelorg);
	if (rotated)
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (modelorg, temp);
		AngleVectors (e->angles, forward, right, up);
		modelorg[0] = DotProduct (temp, forward);
		modelorg[1] = -DotProduct (temp, right);
		modelorg[2] = DotProduct (temp, up);
	}

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

// calculate dynamic lighting for bmodel if it's not an
// instanced model
	if (clmodel->firstmodelsurface != 0 && !gl_flashblend.integer)
	{
		for (k = 0; k < MAX_DLIGHTS; k++)
		{
			if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
				continue;

			R_MarkLights (&cl_dlights[k], 1<<k,
					clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	glPushMatrix_fp ();
	e->angles[0] = -e->angles[0];	// stupid quake bug
	e->angles[2] = -e->angles[2];	// stupid quake bug
	/* hack the origin to prevent bmodel z-fighting
	 * http://forums.inside3d.com/viewtopic.php?t=1350 */
	if (gl_zfix.integer)
	{
		e->origin[0] -= DIST_EPSILON;
		e->origin[1] -= DIST_EPSILON;
		e->origin[2] -= DIST_EPSILON;
	}
	R_RotateForEntity (e);
	/* un-hack the origin */
	if (gl_zfix.integer)
	{
		e->origin[0] += DIST_EPSILON;
		e->origin[1] += DIST_EPSILON;
		e->origin[2] += DIST_EPSILON;
	}
	e->angles[0] = -e->angles[0];	// stupid quake bug
	e->angles[2] = -e->angles[2];	// stupid quake bug

	//
	// draw texture
	//
	
	//Fog_DisableGFog();
	//glDisable_fp(GL_BLEND);
	//psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	//glEnable_fp(GL_BLEND);
	//Fog_EnableGFog();

	//glEnable_fp(GL_ALPHA_TEST); // Flip on alpha test

	if (!Translucent &&
		(e->drawflags & MLS_ABSLIGHT) != MLS_ABSLIGHT &&
		!gl_mtexable)
	{
		Fog_DisableGFog();
		for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
		{
			// find which side of the node we are on
			pplane = psurf->plane;

			dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

			// draw the polygon
			if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
				(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
			{
				//glDepthFunc_fp(GL_EQUAL);
				//glDepthMask_fp(1);
				R_RenderBrushPoly(e, psurf, false, false); //shan door stuff
				//glDepthMask_fp(0);
				//glDepthFunc_fp(GL_LEQUAL);
			}
		}
		Fog_EnableGFog();

		glDepthFunc_fp(GL_EQUAL);
		glDepthMask_fp(0);
		Fog_StartAdditive();

		R_BlendLightmaps(Translucent); //shan door shading

		Fog_StopAdditive();
		glDepthMask_fp(1);
		glDepthFunc_fp(GL_LEQUAL);


		if (Fog_GetDensity() > 0.00)
		{
			psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
			glEnable_fp(GL_BLEND);
			glBlendFunc_fp(GL_ONE, GL_ONE); //add
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f_fp(0, 0, 0);

			for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
			{
				// find which side of the node we are on
				pplane = psurf->plane;

				dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

				// draw the polygon
				if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
					(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				{
					glDepthFunc_fp(GL_EQUAL);
					glDepthMask_fp(1);
					R_RenderBrushPoly(e, psurf, false, true); //shan door stuff
					glDepthMask_fp(0);
					glDepthFunc_fp(GL_LEQUAL);
				}
			}
			glColor3f_fp(1, 1, 1);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}

		glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable_fp(GL_BLEND);
		glDepthMask_fp(GL_TRUE);

	}
	else
	{
		if (Translucent)
		{
			glDepthMask_fp(0);
			if ((e->drawflags & MLS_ABSLIGHT) != MLS_ABSLIGHT)
			{
				//glDepthFunc_fp(GL_EQUAL);
				//one pass with no fog
				//Fog_DisableGFog();
				//Fog_StartAdditive();
				glBlendFunc_fp(GL_ONE, GL_ONE);
				for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
				{
					// find which side of the node we are on
					pplane = psurf->plane;

					dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

					// draw the polygon
					if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
						(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
					{
						R_RenderBrushPoly(e, psurf, false, false);
					}
				}
				glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				//Fog_StopAdditive();
				//Fog_EnableGFog();
				//glDepthMask_fp(1);
				//glDepthFunc_fp(GL_LEQUAL);
			}
			else
			{
				//one modulate pass with black fog
				glDisable_fp(GL_BLEND);
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				Fog_StartAdditive();
				psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
				for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
				{
					// find which side of the node we are on
					pplane = psurf->plane;

					dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

					// draw the polygon
					if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
						(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
					{
						R_RenderBrushPoly(e, psurf, true, false);
					}
				}
				Fog_StopAdditive();
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glEnable_fp(GL_BLEND);
			}

			//one additive pass with black geometry and normal fog
			//glEnable_fp(GL_BLEND);
			//glDisable_fp(GL_BLEND);
			glBlendFunc_fp(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR);
			glDepthMask_fp(GL_FALSE);
			glDisable_fp(GL_TEXTURE_2D);
			glColor3f_fp(0, 0, 0);
			psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
			for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
			{
				// find which side of the node we are on
				pplane = psurf->plane;

				dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

				// draw the polygon
				if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
					(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				{
					//R_RenderBrushPoly(e, psurf, false, true);
				}
			}
			glColor3f_fp(1, 1, 1);
			glEnable_fp(GL_TEXTURE_2D);
			glDepthMask_fp(GL_TRUE);
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			//glDisable_fp(GL_BLEND);
			//glEnable_fp(GL_BLEND);
			
			glDepthMask_fp(1);
			//glDepthFunc_fp(GL_LEQUAL);
		}
		else
		{
			if (gl_mtexable)
			{
				//Fog_DisableGFog();
				for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
				{
					// find which side of the node we are on
					pplane = psurf->plane;

					dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

					// draw the polygon
					if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
						(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
					{
						R_RenderBrushPoly(e, psurf, false, false);
					}
				}
				//Fog_EnableGFog();
			}
			else
			{
				//glDepthMask_fp(0);
				//one pass with no fog
				//Fog_DisableGFog();
				for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
				{
					// find which side of the node we are on
					pplane = psurf->plane;

					dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

					// draw the polygon
					if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
						(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
					{
						R_RenderBrushPoly(e, psurf, false, false);
					}
				}
				//Fog_EnableGFog();

				//one modulate pass with black fog and shading?
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				Fog_StartAdditive();
				psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
				for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
				{
					// find which side of the node we are on
					pplane = psurf->plane;

					dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

					// draw the polygon
					if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
						(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
					{
						//R_RenderBrushPoly(e, psurf, false, unlit);
					}
				}
				Fog_StopAdditive();
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);


				//one additive pass with black geometry and normal fog
				glEnable_fp(GL_BLEND);
				glBlendFunc_fp(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR);
				glDepthMask_fp(GL_FALSE);
				glDisable_fp(GL_TEXTURE_2D);
				glColor3f_fp(0, 0, 0);
				psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
				for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
				{
					// find which side of the node we are on
					pplane = psurf->plane;

					dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

					// draw the polygon
					if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
						(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
					{
						//R_RenderBrushPoly(e, psurf, false, true);
					}
				}
				glColor3f_fp(1, 1, 1);
				glEnable_fp(GL_TEXTURE_2D);
				glDepthMask_fp(GL_TRUE);
				glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable_fp(GL_BLEND);
				//glDepthMask_fp(1);
			}
		}
	}
	glDisable_fp(GL_ALPHA_TEST); // Flip on alpha test

	//Fog_EnableGFog();

/*
	//one modulate pass with black fog and shading?
	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	Fog_StartAdditive();

	if (!Translucent && !unlit &&
		(e->drawflags & MLS_ABSLIGHT) != MLS_ABSLIGHT &&
		!gl_mtexable)
	{
		glDepthFunc_fp(GL_EQUAL);
		glDepthMask_fp(0);

		R_BlendLightmaps(Translucent);

		glDepthMask_fp(1);
		glDepthFunc_fp(GL_LEQUAL);
	}
	else
	{
		if (!Translucent)
		{
			psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
			for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
			{
				// find which side of the node we are on
				pplane = psurf->plane;

				dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

				// draw the polygon
				if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
					(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
				{
					R_RenderBrushPoly(e, psurf, false, unlit);
				}
			}
		}
	}
	Fog_StopAdditive();
	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	*/
	/*
	//one additive pass with black geometry and normal fog
	glEnable_fp(GL_BLEND);
	glBlendFunc_fp(GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR);
	glDepthMask_fp(GL_FALSE);
	glDisable_fp(GL_TEXTURE_2D);
	glColor3f_fp(0, 0, 0);
	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
	{
		// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_RenderBrushPoly(e, psurf, false, true);
		}
	}
	glColor3f_fp(1, 1, 1);
	glEnable_fp(GL_TEXTURE_2D);
	glDepthMask_fp(GL_TRUE);
	glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable_fp(GL_BLEND);
*/

	/*
	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];
	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_RenderBrushPoly (e, psurf, false, unlit);
		}
	}
	

	if (!Translucent && !unlit &&
		(e->drawflags & MLS_ABSLIGHT) != MLS_ABSLIGHT &&
		!gl_mtexable)
	{
		glDepthFunc_fp(GL_EQUAL);
		glDepthMask_fp(0);

		R_BlendLightmaps(Translucent);

		glDepthMask_fp(1);
		glDepthFunc_fp(GL_LEQUAL);
	}
	*/
	glPopMatrix_fp ();
#if 0 /* see above... */
	if (gl_zfix.integer && !gl_ztrick.integer)
	{
		glDisable_fp(GL_POLYGON_OFFSET_FILL);
		glDisable_fp(GL_POLYGON_OFFSET_LINE);
	}
#endif /* #if 0 */
}


/*
=============================================================

WORLD MODEL

=============================================================
*/


//==============================================================================
//
// SETUP CHAINS
//
//==============================================================================

/*
================
R_ClearTextureChains -- ericw

clears texture chains for all textures used by the given model, and also
clears the lightmap chains
================
*/
void R_ClearTextureChains(qmodel_t *mod, texchain_t chain)
{
	int i;

	// set all chains to null
	for (i = 0; i < mod->numtextures; i++)
		if (mod->textures[i])
			mod->textures[i]->texturechains[chain] = NULL;

	// clear lightmap chains
	memset(lightmap_polys, 0, sizeof(lightmap_polys));
}

/*
================
R_ChainSurface -- ericw -- adds the given surface to its texture chain
================
*/
void R_ChainSurface(msurface_t *surf, texchain_t chain)
{
	surf->texturechain = surf->texinfo->texture->texturechains[chain];
	surf->texinfo->texture->texturechains[chain] = surf;
}


/*
================
R_RecursiveWorldNode

similar to R_MarkSurfaces
================
*/
static void R_RecursiveWorldNode (mnode_t *node)
{
	int		c, side;
	mplane_t	*plane;
	msurface_t	*surf, **mark;
	mleaf_t		*pleaf;
	double		dot;

	if (node->contents == CONTENTS_SOLID)
		return;		// solid

	if (node->visframe != r_visframecount)
		return;
	if (R_CullBox (node->minmaxs, node->minmaxs+3))
		return;

// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		pleaf = (mleaf_t *)node;

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

	// deal with model fragments in this leaf
		if (pleaf->efrags)
			R_StoreEfrags (&pleaf->efrags);

		return;
	}

// node is just a decision point, so go down the apropriate sides

// find which side of the node we are on
	plane = node->plane;

	switch (plane->type)
	{
	case PLANE_X:
		dot = modelorg[0] - plane->dist;
		break;
	case PLANE_Y:
		dot = modelorg[1] - plane->dist;
		break;
	case PLANE_Z:
		dot = modelorg[2] - plane->dist;
		break;
	default:
		dot = DotProduct (modelorg, plane->normal) - plane->dist;
		break;
	}

	if (dot >= 0)
		side = 0;
	else
		side = 1;

// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side]);

// draw stuff
	c = node->numsurfaces;

	if (c)
	{
		surf = cl.worldmodel->surfaces + node->firstsurface;

		if (dot < 0 -BACKFACE_EPSILON)
			side = SURF_PLANEBACK;
		else if (dot > BACKFACE_EPSILON)
			side = 0;

		for ( ; c ; c--, surf++)
		{
			if (surf->visframe != r_framecount)
				continue;

			// don't backface underwater surfaces, because they warp
			if (!(surf->flags & SURF_UNDERWATER) && ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)))
				continue;	// wrong side

			// sorting by texture, just store it out
			if (!mirror
				|| surf->texinfo->texture != cl.worldmodel->textures[mirrortexturenum])
			{
				surf->texturechain = surf->texinfo->texture->texturechains[chain_world];
				surf->texinfo->texture->texturechains[chain_world] = surf;
			}
		}
	}

// recurse down the back side
	R_RecursiveWorldNode (node->children[!side]);
}

/*
=============
R_DrawWorld
=============
*/
void R_DrawWorld (void)
{
	if (!r_drawworld.integer)
		return;

	VectorCopy (r_refdef.vieworg, modelorg);

	currenttexture[0] = GL_UNUSED_TEXTURE;

	glColor4f_fp (1.0f,1.0f,1.0f,1.0f);
	memset (lightmap_polys, 0, sizeof(lightmap_polys));
#ifdef QUAKE2
	R_ClearSkyBox ();
#endif

	R_RecursiveWorldNode (cl.worldmodel->nodes);
	DrawTextureChains_New(&r_worldentity);

	// disable multitexturing - just in case
/*	if (gl_mtexable)
	{
		//glActiveTextureARB_fp (GL_TEXTURE1_ARB);
		//glDisable_fp(GL_TEXTURE_2D);
		GL_DisableMultitexture();
		glActiveTextureARB_fp (GL_TEXTURE0_ARB);
		glEnable_fp(GL_TEXTURE_2D);
	}
*/
	//glDepthFunc_fp(GL_EQUAL);
	//glDepthMask_fp(0);

	//if (!gl_mtexable)
	//	R_BlendLightmaps(false);
	//else
	//	R_UpdateLightmaps(false);

	//glDepthMask_fp(1);
	//glDepthFunc_fp(GL_LEQUAL);

#ifdef QUAKE2
	R_DrawSkyBox ();
#endif
}


/*
=============================================================================

LIGHTMAP ALLOCATION

=============================================================================
*/

// returns a texture number and the position inside it
static unsigned int AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	unsigned int	texnum;

	for (texnum = 0; texnum < MAX_LIGHTMAPS; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i = 0; i < BLOCK_WIDTH - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (allocated[texnum][i+j] >= best)
					break;
				if (allocated[texnum][i+j] > best2)
					best2 = allocated[texnum][i+j];
			}
			if (j == w)
			{	// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("%s: full", __thisfunc__);
	return -1;	// shut up the compiler
}


#define	COLINEAR_EPSILON	0.001
static mvertex_t	*r_pcurrentvertbase;
static qmodel_t		*currentmodel;

/*
================
BuildSurfaceDisplayList
================
*/
static void BuildSurfaceDisplayList (msurface_t *fa)
{
	int		i, lindex, lnumverts;
	medge_t		*pedges, *r_pedge;
	float		*vec;
	float		s, t;
	glpoly_t	*poly;

// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = fa->numedges;

	//
	// draw texture
	//
	poly = (glpoly_t *) Hunk_AllocName (sizeof(glpoly_t) + (lnumverts-4) * VERTEXSIZE*sizeof(float), "poly");
	poly->next = fa->polys;
	poly->flags = fa->flags;
	fa->polys = poly;
	poly->numverts = lnumverts;

	for (i = 0; i < lnumverts; i++)
	{
		lindex = currentmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = r_pcurrentvertbase[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = r_pcurrentvertbase[r_pedge->v[1]].position;
		}
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s /= fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t /= fa->texinfo->texture->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		//
		// lightmap texture coordinates
		//
		s = DotProduct (vec, fa->texinfo->vecs[0]) + fa->texinfo->vecs[0][3];
		s -= fa->texturemins[0];
		s += fa->light_s*16;
		s += 8;
		s /= BLOCK_WIDTH*16; //fa->texinfo->texture->width;

		t = DotProduct (vec, fa->texinfo->vecs[1]) + fa->texinfo->vecs[1][3];
		t -= fa->texturemins[1];
		t += fa->light_t*16;
		t += 8;
		t /= BLOCK_HEIGHT*16; //fa->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	//
	// remove co-linear points - Ed
	//
	if (!gl_keeptjunctions.integer && !(fa->flags & SURF_UNDERWATER))
	{
		for (i = 0; i < lnumverts; ++i)
		{
			vec3_t	v1, v2;
			float	*prev, *curr, *next;

			prev = poly->verts[(i + lnumverts - 1) % lnumverts];
			curr = poly->verts[i];
			next = poly->verts[(i + 1) % lnumverts];

			VectorSubtract(curr, prev, v1);
			VectorNormalize(v1);
			VectorSubtract(next, prev, v2);
			VectorNormalize(v2);

			// skip co-linear points
			if ((fabs(v1[0] - v2[0]) <= COLINEAR_EPSILON) &&
			    (fabs(v1[1] - v2[1]) <= COLINEAR_EPSILON) &&
			    (fabs(v1[2] - v2[2]) <= COLINEAR_EPSILON))
			{
				int		j, k;
				for (j = i + 1; j < lnumverts; ++j)
				{
					for (k = 0; k < VERTEXSIZE; ++k)
						poly->verts[j - 1][k] = poly->verts[j][k];
				}
				--lnumverts;
				// retry next vertex next time, which is now current vertex
				--i;
			}
		}
	}

	poly->numverts = lnumverts;
}

/*
========================
GL_CreateSurfaceLightmap
========================
*/
static void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int	smax, tmax;
	byte	*base;

	if (surf->flags & (SURF_DRAWSKY|SURF_DRAWTURB))
		return;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;

	surf->lightmaptexturenum = AllocBlock (smax, tmax, &surf->light_s, &surf->light_t);
	base = lightmaps + surf->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
	base += (surf->light_t * BLOCK_WIDTH + surf->light_s) * lightmap_bytes;
	R_BuildLightMap (surf, base, BLOCK_WIDTH*lightmap_bytes);
}


/*
==================
GL_BuildLightmaps

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps (void)
{
	int		i, j;
	qmodel_t	*m;
	char	name[16];
	byte	*data;

	memset (allocated, 0, sizeof(allocated));

	r_framecount = 1;		// no dlightcache

	//johnfitz -- null out array (the gltexture objects themselves were already freed by Mod_ClearAll)
	for (i = 0; i < MAX_LIGHTMAPS; i++)
		lightmap_textures[i] = NULL;
	//johnfitz

	switch (gl_lightmap_format)
	{
	case GL_LUMINANCE:
		lightmap_bytes = 1;
		break;
	case GL_RGBA:
		lightmap_bytes = 4;
		break;
	default:
		Sys_Error("GL_BuildLightmaps: bad lightmap format");
	}

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m)
			break;
		if (m->name[0] == '*')
			continue;

		r_pcurrentvertbase = m->vertexes;
		currentmodel = m;

		for (i = 0; i < m->numsurfaces; i++)
		{
			if (m->surfaces[i].flags & SURF_DRAWTURB)
				continue;
#ifndef QUAKE2
			if (m->surfaces[i].flags & SURF_DRAWSKY)
				continue;
#endif
			GL_CreateSurfaceLightmap(m->surfaces + i);
			if (!draw_reinit)
				BuildSurfaceDisplayList(m->surfaces + i);
		}
	}

	//if (gl_mtexable)
	//	glActiveTextureARB_fp (GL_TEXTURE1_ARB);
	//if (gl_mtexable)
	//	GL_EnableMultitexture();

	//
	// upload all lightmaps that were filled
	//
	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		/*if (!allocated[i][0])
			break;		// no more used

		
		lightmap_modified[i] = false;
		GL_Bind(lightmap_textures[i]);
		glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D_fp (GL_TEXTURE_2D, 0, lightmap_bytes, BLOCK_WIDTH,
				BLOCK_HEIGHT, 0, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps + i*BLOCK_WIDTH*BLOCK_HEIGHT*lightmap_bytes);
				*/

		if (!allocated[i][0])
			break;		// no more used
		lightmap_modified[i] = false;
		lightmap_rectchange[i].l = BLOCK_WIDTH;
		lightmap_rectchange[i].t = BLOCK_HEIGHT;
		lightmap_rectchange[i].w = 0;
		lightmap_rectchange[i].h = 0;

		//johnfitz -- use texture manager
		sprintf(name, "lightmap%03i", i);
		data = lightmaps + i * BLOCK_WIDTH*BLOCK_HEIGHT*lightmap_bytes;
		lightmap_textures[i] = TexMgr_LoadImage(cl.worldmodel, name, BLOCK_WIDTH, BLOCK_HEIGHT,
			SRC_LIGHTMAP, data, "", (src_offset_t)data, TEXPREF_LINEAR | TEXPREF_NOPICMIP);
		//johnfitz
	}

	//if (gl_mtexable)
	//	glActiveTextureARB_fp (GL_TEXTURE0_ARB);
	//if (gl_mtexable)
	//	GL_DisableMultitexture();
}

