/*
	r_surf.c
	surface-related refresh code

	$Id: gl_rsurf.c,v 1.34 2007-07-08 11:55:36 sezero Exp $
*/

#include "quakedef.h"

int		gl_lightmap_format = GL_RGBA;
cvar_t		gl_lightmapfmt = {"gl_lightmapfmt", "GL_RGBA", CVAR_ARCHIVE};
int		lightmap_bytes = 4;		// 1, 2, or 4. default is 4 for GL_RGBA
GLuint		lightmap_textures;

static unsigned	blocklights[18*18];
static unsigned	blocklightscolor[18*18*3];	// colored light support. *3 for RGB to the definitions at the top

#define	BLOCK_WIDTH	128
#define	BLOCK_HEIGHT	128

typedef struct glRect_s {
	unsigned char l,t,w,h;
} glRect_t;

static glpoly_t	*lightmap_polys[MAX_LIGHTMAPS];
qboolean	lightmap_modified[MAX_LIGHTMAPS];
static glRect_t	lightmap_rectchange[MAX_LIGHTMAPS];

static int	allocated[MAX_LIGHTMAPS][BLOCK_WIDTH];

// the lightmap texture data needs to be kept in
// main memory so texsubimage can update properly
static byte	lightmaps[4*MAX_LIGHTMAPS*BLOCK_WIDTH*BLOCK_HEIGHT];
extern qboolean draw_reinit;


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
	unsigned	*bl;

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
					bl[0] += (int) (brightness * cred);
					bl[1] += (int) (brightness * cgreen);
					bl[2] += (int) (brightness * cblue);

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
at every level change and at first video initialization.
Best to be called from Mod_LoadLighting() in gl_model.c
===============
*/
void GL_SetupLightmapFmt (qboolean check_cmdline)
{
	// only GL_LUMINANCE and GL_RGBA are actually supported
	// commenting out other options
	if (!Q_strcasecmp(gl_lightmapfmt.string, "GL_LUMINANCE"))
		gl_lightmap_format = GL_LUMINANCE;
	else if (!Q_strcasecmp(gl_lightmapfmt.string, "GL_RGBA"))
		gl_lightmap_format = GL_RGBA;
#if 0
	else if (!Q_strcasecmp(gl_lightmapfmt.string, "GL_ALPHA"))
		gl_lightmap_format = GL_ALPHA;
	else if (!Q_strcasecmp(gl_lightmapfmt.string, "GL_INTENSITY"))
		gl_lightmap_format = GL_INTENSITY;
#endif
	else
	{
		gl_lightmap_format = GL_RGBA;
		Cvar_Set ("gl_lightmapfmt", "GL_RGBA");
	}

	// check for commandline overrides
	if (check_cmdline)
	{
		if (COM_CheckParm ("-lm_1"))
		{
			gl_lightmap_format = GL_LUMINANCE;
			Cvar_Set ("gl_lightmapfmt", "GL_LUMINANCE");
		}
		else if (COM_CheckParm ("-lm_4"))
		{
			gl_lightmap_format = GL_RGBA;
			Cvar_Set ("gl_lightmapfmt", "GL_RGBA");
		}
#if 0
//		else if (COM_CheckParm ("-lm_2"))
//		{
//			gl_lightmap_format = GL_RGBA4;
//			Cvar_Set ("gl_lightmapfmt", "GL_RGBA4");
//		}
		else if (COM_CheckParm ("-lm_a"))
		{
			gl_lightmap_format = GL_ALPHA;
			Cvar_Set ("gl_lightmapfmt", "GL_ALPHA");
		}
		else if (COM_CheckParm ("-lm_i"))
		{
			gl_lightmap_format = GL_INTENSITY;
			Cvar_Set ("gl_lightmapfmt", "GL_INTENSITY");
		}
#endif
	}

	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		lightmap_bytes = 4;
		break;
//	case GL_RGBA4:
//		lightmap_bytes = 2;
//		break;
	case GL_LUMINANCE:
	case GL_INTENSITY:
	case GL_ALPHA:
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
static void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int		smax, tmax;
	int		t, r, s, q;
	int		i, j, size;
	byte		*lightmap;
	unsigned	scale;
	int		maps;
	unsigned	*bl, *blcr, *blcg, *blcb;


	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	size = smax*tmax;
	lightmap = surf->samples;

// set to full bright if no light data
	if (r_fullbright.integer || !cl.worldmodel->lightdata)
	{
		for (i = 0; i < size; i++)
		{
			if (gl_lightmap_format == GL_RGBA)
				blocklightscolor[i*3+0] =
				blocklightscolor[i*3+1] =
				blocklightscolor[i*3+2] = 65280;
			else
				blocklights[i] = 255*256;
		}
		goto store;
	}

// clear to no light
	for (i = 0; i < size; i++)
	{
		if (gl_lightmap_format == GL_RGBA)
			blocklightscolor[i*3+0] =
			blocklightscolor[i*3+1] =
			blocklightscolor[i*3+2] = 0;
		else
			blocklights[i] = 0;
	}

// add all the lightmaps
	if (lightmap)
	{
		for (maps = 0 ; maps < MAXLIGHTMAPS && surf->styles[maps] != 255 ; maps++)
		{
			scale = d_lightstylevalue[surf->styles[maps]];
			surf->cached_light[maps] = scale;	// 8.8 fraction

			if (gl_lightmap_format == GL_RGBA)
			{
				for (i = 0, j = 0; i < size; i++)
				{
					blocklightscolor[i*3+0] += lightmap[j] * scale;
					blocklightscolor[i*3+1] += lightmap[++j] * scale;
					blocklightscolor[i*3+2] += lightmap[++j] * scale;
					j++;
				}

				lightmap += size * 3;
			}
			else
			{
				for (i = 0; i < size; i++)
					blocklights[i] += lightmap[i] * scale;
				lightmap += size;	// skip to next lightmap
			}
		}
	}

// add all the dynamic lights
	if (surf->dlightframe == r_framecount)
		R_AddDynamicLights (surf);

// bound, invert, and shift
store:
	switch (gl_lightmap_format)
	{
	case GL_RGBA:
		stride -= (smax<<2);

		blcr = &blocklightscolor[0];
		blcg = &blocklightscolor[1];
		blcb = &blocklightscolor[2];

		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				q = *blcr;
				q >>= 7;
				r = *blcg;
				r >>= 7;
				s = *blcb;
				s >>= 7;

				if (q > 255)
					q = 255;
				if (r > 255)
					r = 255;
				if (s > 255)
					s = 255;

				if (gl_coloredlight.integer)
				{
					dest[0] = q; //255 - q;
					dest[1] = r; //255 - r;
					dest[2] = s; //255 - s;
					dest[3] = 255; //(q+r+s)/3;
				}
				else
				{
					t = (int) ( ((float)q * 0.33f) + ((float)s * 0.33f) + ((float)r * 0.33f) );

					if (t > 255)
						t = 255;
					dest[0] = t;
					dest[1] = t;
					dest[2] = t;
					dest[3] = 255; //t;
				}

				dest += 4;

				blcr += 3;
				blcg += 3;
				blcb += 3;
			}
		}
		break;
	case GL_ALPHA:
	case GL_LUMINANCE:
	case GL_INTENSITY:
		bl = blocklights;
		for (i = 0; i < tmax; i++, dest += stride)
		{
			for (j = 0; j < smax; j++)
			{
				t = *bl++;
				t >>= 7;
				if (t > 255)
					t = 255;
				dest[j] = 255-t;
			}
		}
		break;
	default:
		Sys_Error ("Bad lightmap format");
	}
}


/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
static texture_t *R_TextureAnimation (texture_t *base)
{
	int		reletive;
	int		count;

	if (currententity->frame)
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

		if (gl_waterwarp.integer)
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

		if (gl_waterwarp.integer)
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

		if (gl_waterwarp.integer)
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
static void DrawGLPoly (glpoly_t *p)
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

static void DrawGLPolyMTex (glpoly_t *p)
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
R_BlendLightmaps
================
*/
static void R_BlendLightmaps (qboolean Translucent)
{
	unsigned int		i;
	int			j;
	glpoly_t	*p;
	float		*v;
	glRect_t	*theRect;

	if (r_fullbright.integer)
		return;

	if (!Translucent)
		glDepthMask_fp (0);	// don't bother writing Z

	if (gl_lightmap_format == GL_LUMINANCE)
	{
		glBlendFunc_fp (GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
	}
	else if (gl_lightmap_format == GL_INTENSITY)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f_fp (0.0f,0.0f,0.0f,1.0f);
		glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else if (gl_lightmap_format == GL_RGBA)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f_fp (1.0f,1.0f,1.0f, 1.0f);
		glBlendFunc_fp (GL_ZERO, GL_SRC_COLOR);
	}

	if (!r_lightmap.integer)
	{
		glEnable_fp (GL_BLEND);
	}

	if (!lightmap_textures)
	{
		// if lightmaps were hosed in a video mode change, make
		// sure we allocate new slots for lightmaps, otherwise
		// we'll probably overwrite some other existing textures.
		lightmap_textures = texture_extension_number;
		texture_extension_number += MAX_LIGHTMAPS;
	}

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		p = lightmap_polys[i];
		if (!p)
			continue;	// skip if no lightmap

		GL_Bind(lightmap_textures+i);

		if (lightmap_modified[i])
		{
			// if current lightmap was changed reload it
			// and mark as not changed.
			lightmap_modified[i] = false;
			theRect = &lightmap_rectchange[i];
			// make sure filtering modes are correct on display
			// mode changes and gl_texturemode commands.
			glTexParameterf_fp (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
			glTexParameterf_fp (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			glTexSubImage2D_fp(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH,
					theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
					lightmaps + (i* BLOCK_HEIGHT + theRect->t)*BLOCK_WIDTH*lightmap_bytes);
			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
		}

		for ( ; p ; p = p->chain)
		{
			//if (p->flags & SURF_UNDERWATER)
			if ( ( (r_viewleaf->contents == CONTENTS_EMPTY && (p->flags & SURF_UNDERWATER)) ||
					(r_viewleaf->contents != CONTENTS_EMPTY && !(p->flags & SURF_UNDERWATER)) )
				    && !(p->flags & SURF_DONTWARP) )
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

	if (gl_lightmap_format == GL_LUMINANCE)
	{
		glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else if (gl_lightmap_format == GL_INTENSITY)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor4f_fp (1.0f,1.0f,1.0f,1.0f);
	}
	else if (gl_lightmap_format == GL_RGBA)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}

	if (!Translucent)
		glDepthMask_fp (1);	// back to normal Z buffering
}

static void R_UpdateLightmaps (qboolean Translucent)
{
	unsigned int		i;
	glpoly_t	*p;
	glRect_t	*theRect;

	if (r_fullbright.integer)
		return;

	glActiveTextureARB_fp (GL_TEXTURE1_ARB);

	if (!lightmap_textures)
	{
		// if lightmaps were hosed in a video mode change, make
		// sure we allocate new slots for lightmaps, otherwise
		// we'll probably overwrite some other existing textures.
		lightmap_textures = texture_extension_number;
		texture_extension_number += MAX_LIGHTMAPS;
	}

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		p = lightmap_polys[i];
		if (!p)
			continue;	// skip if no lightmap

		GL_Bind(lightmap_textures+i);

		if (lightmap_modified[i])
		{
			// if current lightmap was changed reload it
			// and mark as not changed.
			lightmap_modified[i] = false;
			theRect = &lightmap_rectchange[i];
			// make sure filtering modes are correct on display
			// mode changes and gl_texturemode commands.
			glTexParameterf_fp (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
			glTexParameterf_fp (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
			glTexSubImage2D_fp(GL_TEXTURE_2D, 0, 0, theRect->t, BLOCK_WIDTH,
					theRect->h, gl_lightmap_format, GL_UNSIGNED_BYTE,
					lightmaps + (i* BLOCK_HEIGHT + theRect->t)*BLOCK_WIDTH*lightmap_bytes);
			theRect->l = BLOCK_WIDTH;
			theRect->t = BLOCK_HEIGHT;
			theRect->h = 0;
			theRect->w = 0;
		}
	}

	glActiveTextureARB_fp (GL_TEXTURE0_ARB);
}


/*
================
R_RenderBrushPoly
================
*/
void R_RenderBrushPoly (msurface_t *fa, qboolean override)
{
	texture_t	*t;
	byte		*base;
	int			maps;
	glRect_t	*theRect;
	int			smax, tmax;
	float		intensity = 1.0f, alpha_val = 1.0f;

	c_brush_polys++;

#if 0
	if (currententity->drawflags & DRF_TRANSLUCENT)
	{
		glEnable_fp (GL_BLEND);
		glColor4f_fp (1,1,1,r_wateralpha.value);
		// rjr
	}
	if ((currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
	{
		// rjr
	}
#endif

	if (gl_multitexture.integer && gl_mtexable)
		glActiveTextureARB_fp(GL_TEXTURE0_ARB);

	if (currententity->drawflags & DRF_TRANSLUCENT)
	{
		glEnable_fp (GL_BLEND);
	//	glColor4f_fp (1,1,1,r_wateralpha.value);
		alpha_val = r_wateralpha.value;
		// rjr

		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		intensity = 1.0;
	}
	if ((currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
	{
		// currententity->abslight   0 - 255
		// rjr
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		intensity = ( float )currententity->abslight / 255.0f;
	//	intensity = 0;
	}

	if (!override)
		glColor4f_fp( intensity, intensity, intensity, alpha_val );

	if (fa->flags & SURF_DRAWSKY)
	{	// warp texture, no lightmaps
		EmitBothSkyLayers (fa);
		return;
	}

	t = R_TextureAnimation (fa->texinfo->texture);
	GL_Bind (t->gl_texturenum);

	if (fa->flags & SURF_DRAWTURB)
	{	// warp texture, no lightmaps
		EmitWaterPolys (fa);
		return;
	}

	if (gl_multitexture.integer && gl_mtexable)
	{
		if ((currententity->drawflags & DRF_TRANSLUCENT) ||
		    (currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
		{
			if ( ( (r_viewleaf->contents == CONTENTS_EMPTY && (fa->flags & SURF_UNDERWATER)) ||
					(r_viewleaf->contents != CONTENTS_EMPTY && !(fa->flags & SURF_UNDERWATER)) )
				    && !(fa->flags & SURF_DONTWARP) )
				DrawGLWaterPoly (fa->polys);
			else
				DrawGLPoly (fa->polys);
		}
		else
		{
			glActiveTextureARB_fp(GL_TEXTURE1_ARB);

			if (gl_lightmap_format == GL_LUMINANCE)
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
			else
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

			glEnable_fp(GL_TEXTURE_2D);
			GL_Bind (lightmap_textures + fa->lightmaptexturenum);
			//glEnable_fp (GL_BLEND);

			if ( ( (r_viewleaf->contents == CONTENTS_EMPTY && (fa->flags & SURF_UNDERWATER)) ||
					(r_viewleaf->contents != CONTENTS_EMPTY && !(fa->flags & SURF_UNDERWATER)) )
				    && !(fa->flags & SURF_DONTWARP) )
				DrawGLWaterPolyMTexLM (fa->polys);
			else
				DrawGLPolyMTex (fa->polys);

			glDisable_fp(GL_TEXTURE_2D);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			//glDisable_fp (GL_BLEND);

			glActiveTextureARB_fp(GL_TEXTURE0_ARB);
		}
	}
	else
	{
		if ( ( (r_viewleaf->contents == CONTENTS_EMPTY && (fa->flags & SURF_UNDERWATER)) ||
				(r_viewleaf->contents != CONTENTS_EMPTY && !(fa->flags & SURF_UNDERWATER)) )
			    && !(fa->flags & SURF_DONTWARP) )
			DrawGLWaterPoly (fa->polys);
		else
			DrawGLPoly (fa->polys);
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
			theRect = &lightmap_rectchange[fa->lightmaptexturenum];
			if (fa->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - fa->light_t;
				theRect->t = fa->light_t;
			}
			if (fa->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - fa->light_s;
				theRect->l = fa->light_s;
			}
			smax = (fa->extents[0] >> 4) + 1;
			tmax = (fa->extents[1] >> 4) + 1;
			if ((theRect->w + theRect->l) < (fa->light_s + smax))
				theRect->w = (fa->light_s-theRect->l)+smax;
			if ((theRect->h + theRect->t) < (fa->light_t + tmax))
				theRect->h = (fa->light_t-theRect->t)+tmax;
			base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
			base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
			R_BuildLightMap (fa, base, BLOCK_WIDTH*lightmap_bytes);
		}
	}

	if ((currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT ||
	    (currententity->drawflags & DRF_TRANSLUCENT))
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}
}

void R_RenderBrushPolyMTex (msurface_t *fa, qboolean override)
{
	texture_t	*t;
	byte		*base;
	int			maps;
	glRect_t	*theRect;
	int			smax, tmax;
	float		intensity = 1.0f, alpha_val = 1.0f;

	c_brush_polys++;

	glActiveTextureARB_fp(GL_TEXTURE0_ARB);

	if (currententity->drawflags & DRF_TRANSLUCENT)
	{
		glEnable_fp (GL_BLEND);

		alpha_val = r_wateralpha.value;

		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		intensity = 1.0;
	}
	else
	{
		/* KIERO: Seems it's enabled when we enter here. */
		glDisable_fp (GL_BLEND);
	}

	if ((currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		intensity = ( float )currententity->abslight / 255.0f;
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
		EmitBothSkyLayers (fa);
		return;
	}

	glActiveTextureARB_fp(GL_TEXTURE0_ARB);
	t = R_TextureAnimation (fa->texinfo->texture);
	GL_Bind (t->gl_texturenum);

	if (fa->flags & SURF_DRAWTURB)
	{
		glColor4f_fp( 1.0f, 1.0f, 1.0f, 1.0f );
		EmitWaterPolys (fa);
		//return;
	}
	else
	{
		if ((currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT)
		{
			glActiveTextureARB_fp(GL_TEXTURE0_ARB);

			if ( ( (r_viewleaf->contents == CONTENTS_EMPTY && (fa->flags & SURF_UNDERWATER)) ||
					(r_viewleaf->contents != CONTENTS_EMPTY && !(fa->flags & SURF_UNDERWATER)) )
				    && !(fa->flags & SURF_DONTWARP) )
				DrawGLWaterPoly (fa->polys);
			else
				DrawGLPoly (fa->polys);
		}
		else
		{
			glActiveTextureARB_fp(GL_TEXTURE1_ARB);
			GL_Bind (lightmap_textures + fa->lightmaptexturenum);

			if ( ( (r_viewleaf->contents == CONTENTS_EMPTY && (fa->flags & SURF_UNDERWATER)) ||
					(r_viewleaf->contents != CONTENTS_EMPTY && !(fa->flags & SURF_UNDERWATER)) )
				    && !(fa->flags & SURF_DONTWARP) )
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
				theRect = &lightmap_rectchange[fa->lightmaptexturenum];
				if (fa->light_t < theRect->t)
				{
					if (theRect->h)
						theRect->h += theRect->t - fa->light_t;
					theRect->t = fa->light_t;
				}
				if (fa->light_s < theRect->l)
				{
					if (theRect->w)
						theRect->w += theRect->l - fa->light_s;
					theRect->l = fa->light_s;
				}
				smax = (fa->extents[0] >> 4) + 1;
				tmax = (fa->extents[1] >> 4) + 1;
				if ((theRect->w + theRect->l) < (fa->light_s + smax))
					theRect->w = (fa->light_s-theRect->l)+smax;
				if ((theRect->h + theRect->t) < (fa->light_t + tmax))
					theRect->h = (fa->light_t-theRect->t)+tmax;
				base = lightmaps + fa->lightmaptexturenum*lightmap_bytes*BLOCK_WIDTH*BLOCK_HEIGHT;
				base += fa->light_t * BLOCK_WIDTH * lightmap_bytes + fa->light_s * lightmap_bytes;
				R_BuildLightMap (fa, base, BLOCK_WIDTH*lightmap_bytes);
			}
		}
	}

	glActiveTextureARB_fp(GL_TEXTURE0_ARB);

	if ((currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT ||
	    (currententity->drawflags & DRF_TRANSLUCENT))
	{
		glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
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

	glDepthMask_fp( 0 );

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
		s = t->texturechain;
		if (!s)
			continue;
		if ( !(s->flags & SURF_DRAWTURB) )
			continue;

		//if ((s->flags & SURF_DRAWTURB) && (s->flags & SURF_TRANSLUCENT))
		if (s->flags & SURF_TRANSLUCENT)
			glColor4f_fp (1,1,1,r_wateralpha.value);
		else
			glColor4f_fp (1,1,1,1);

		// set modulate mode explicitly
		GL_Bind (t->gl_texturenum);

		for ( ; s ; s = s->texturechain)
			EmitWaterPolys (s);

		t->texturechain = NULL;
	}

	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	glColor4f_fp (1,1,1,1);
	glDisable_fp (GL_BLEND);
	glDepthMask_fp (1);
}

/*
================
DrawTextureChains
================
*/
static void DrawTextureChains (void)
{
	int		i;
	msurface_t	*s;
	texture_t	*t;

	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];
		if (!t)
			continue;
		s = t->texturechain;
		if (!s)
			continue;
		if (i == skytexturenum)
			R_DrawSkyChain (s);
		else if (i == mirrortexturenum && r_mirroralpha.value != 1.0)
		{
			R_MirrorChain (s);
			continue;
		}
		else
		{
			if ((s->flags & SURF_DRAWTURB) && r_wateralpha.value != 1.0)
				continue;	// draw translucent water later

			if (((currententity->drawflags & DRF_TRANSLUCENT) ||
				(currententity->drawflags & MLS_ABSLIGHT) == MLS_ABSLIGHT))
			{
				for ( ; s ; s = s->texturechain);
					R_RenderBrushPoly (s, false);
			}
			else if (gl_multitexture.integer && gl_mtexable)
			{
				glActiveTextureARB_fp(GL_TEXTURE0_ARB);
				glEnable_fp(GL_TEXTURE_2D);
				glActiveTextureARB_fp(GL_TEXTURE1_ARB);
				glEnable_fp(GL_TEXTURE_2D);

				if (gl_lightmap_format == GL_LUMINANCE)
					glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
				else
					glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

				glEnable_fp (GL_BLEND);

				for ( ; s ; s = s->texturechain)
					R_RenderBrushPolyMTex (s, false);

				glDisable_fp(GL_TEXTURE_2D);
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
				glDisable_fp (GL_BLEND);
				glActiveTextureARB_fp(GL_TEXTURE0_ARB);
			}
			else
			{
				for ( ; s ; s = s->texturechain)
					R_RenderBrushPoly (s, false);
			}
		}

		t->texturechain = NULL;
	}
}

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e, qboolean Translucent)
{
	int		i, k;
	vec3_t		mins, maxs;
	msurface_t	*psurf;
	float		dot;
	mplane_t	*pplane;
	model_t		*clmodel;
	qboolean	rotated;

	currententity = e;
	currenttexture = GL_UNUSED_TEXTURE;

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
	R_RotateForEntity (e);
	e->angles[0] = -e->angles[0];	// stupid quake bug

	//
	// draw texture
	//
	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
	{
	// find which side of the node we are on
		pplane = psurf->plane;

		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

	// draw the polygon
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_RenderBrushPoly (psurf, false);
		}
	}

	if (!Translucent && 
		(currententity->drawflags & MLS_ABSLIGHT) != MLS_ABSLIGHT &&
		!(gl_multitexture.integer && gl_mtexable))
	{
		R_BlendLightmaps (Translucent);
	}
	glPopMatrix_fp ();
}


/*
=============================================================

WORLD MODEL

=============================================================
*/

/*
================
R_RecursiveWorldNode
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
		//	if ( !(surf->flags & SURF_UNDERWATER) && ( (dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) )
			if ( !( ((r_viewleaf->contents == CONTENTS_EMPTY && (surf->flags & SURF_UNDERWATER)) ||
					(r_viewleaf->contents != CONTENTS_EMPTY && !(surf->flags & SURF_UNDERWATER)) )
				    && !(surf->flags & SURF_DONTWARP)) && ( (dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) )
				continue;	// wrong side

			// sorting by texture, just store it out
			if (!mirror
				|| surf->texinfo->texture != cl.worldmodel->textures[mirrortexturenum])
			{
				surf->texturechain = surf->texinfo->texture->texturechain;
				surf->texinfo->texture->texturechain = surf;
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
	entity_t	ent;

	memset (&ent, 0, sizeof(ent));
	ent.model = cl.worldmodel;

	VectorCopy (r_refdef.vieworg, modelorg);

	currententity = &ent;
	currenttexture = GL_UNUSED_TEXTURE;

	glColor4f_fp (1.0f,1.0f,1.0f,1.0f);
	memset (lightmap_polys, 0, sizeof(lightmap_polys));
#ifdef QUAKE2
	R_ClearSkyBox ();
#endif

	R_RecursiveWorldNode (cl.worldmodel->nodes);

	DrawTextureChains ();

	// disable multitexturing - just in case ...
	if (gl_multitexture.integer && gl_mtexable)
	{
		glActiveTextureARB_fp (GL_TEXTURE1_ARB);
		glDisable_fp(GL_TEXTURE_2D);
		glActiveTextureARB_fp (GL_TEXTURE0_ARB);
		glEnable_fp(GL_TEXTURE_2D);
	}

	if (!gl_multitexture.integer || !gl_mtexable)
		R_BlendLightmaps (false);
	else
		R_UpdateLightmaps (false);

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
static model_t		*currentmodel;

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
	if (!gl_keeptjunctions.integer && !(fa->flags & SURF_UNDERWATER) )
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
			if ((fabs( v1[0] - v2[0] ) <= COLINEAR_EPSILON) &&
				(fabs( v1[1] - v2[1] ) <= COLINEAR_EPSILON) &&
				(fabs( v1[2] - v2[2] ) <= COLINEAR_EPSILON))
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
	model_t	*m;

	memset (allocated, 0, sizeof(allocated));

	r_framecount = 1;		// no dlightcache

	if (!lightmap_textures)
	{
		lightmap_textures = texture_extension_number;
		texture_extension_number += MAX_LIGHTMAPS;
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
			GL_CreateSurfaceLightmap (m->surfaces + i);
			if ( m->surfaces[i].flags & SURF_DRAWTURB )
				continue;
#ifndef QUAKE2
			if ( m->surfaces[i].flags & SURF_DRAWSKY )
				continue;
#endif
			if (!draw_reinit)
				BuildSurfaceDisplayList (m->surfaces + i);
		}
	}

	if (gl_multitexture.integer && gl_mtexable)
		glActiveTextureARB_fp (GL_TEXTURE1_ARB);

	//
	// upload all lightmaps that were filled
	//
	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		if (!allocated[i][0])
			break;		// no more used
		lightmap_modified[i] = false;
		lightmap_rectchange[i].l = BLOCK_WIDTH;
		lightmap_rectchange[i].t = BLOCK_HEIGHT;
		lightmap_rectchange[i].w = 0;
		lightmap_rectchange[i].h = 0;
		GL_Bind(lightmap_textures + (GLuint)i);
		glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
		glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
		glTexImage2D_fp (GL_TEXTURE_2D, 0, lightmap_bytes, BLOCK_WIDTH,
				BLOCK_HEIGHT, 0, gl_lightmap_format, GL_UNSIGNED_BYTE,
				lightmaps + i*BLOCK_WIDTH*BLOCK_HEIGHT*lightmap_bytes);
	}

	if (gl_multitexture.integer && gl_mtexable)
		glActiveTextureARB_fp (GL_TEXTURE0_ARB);
}

