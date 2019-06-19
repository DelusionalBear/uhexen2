/*
 * gl_main.c
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

 //johnfitz -- struct for passing lerp information to drawing functions
typedef struct {
	short pose1;
	short pose2;
	float blend;
	vec3_t origin;
	vec3_t angles;
} lerpdata_t;
//johnfitz

qboolean	overbright; //johnfitz
float	entalpha; //johnfitz
entity_t  *currententity;
static GLuint r_alias_program;

qboolean shading = true; //johnfitz -- if false, disable vertex shading for various reasons (fullbright, r_lightmap, showtris, etc)
int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;

entity_t	r_worldentity;
vec3_t		modelorg, r_entorigin;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

int			c_brush_polys, c_alias_polys;

qboolean	r_cache_thrash;			// compatability

GLuint			currenttexture = GL_UNUSED_TEXTURE;	// to avoid unnecessary texture sets

GLuint			particletexture;	// little dot for particles
GLuint			playertextures[MAX_CLIENTS];	// up to MAX_CLIENTS color translated skins
GLuint			gl_extra_textures[MAX_EXTRA_TEXTURES];   // generic textures for models

int			mirrortexturenum;	// quake texturenum, not gltexturenum
qboolean	mirror;
mplane_t	*mirror_plane;

static float	model_constant_alpha;

static float	r_time1;
static float	r_lasttime1 = 0;

extern qmodel_t	*player_models[MAX_PLAYER_CLASS];

//
// view origin
//
vec3_t		vup, vpn, vright, r_origin;

float		r_world_matrix[16];

//
// screen size info
//
refdef_t	r_refdef;
mleaf_t		*r_viewleaf, *r_oldviewleaf;

texture_t	*r_notexture_mip;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value

int		gl_coloredstatic;	// used to store what type of static light
					// we loaded in Mod_LoadLighting()

static	qboolean AlwaysDrawModel;

cvar_t	r_norefresh = {"r_norefresh", "0", CVAR_NONE};
cvar_t	r_drawentities = {"r_drawentities", "1", CVAR_NONE};
cvar_t	r_drawviewmodel = {"r_drawviewmodel", "1", CVAR_NONE};
cvar_t	r_speeds = {"r_speeds", "0", CVAR_NONE};
cvar_t	r_waterwarp = {"r_waterwarp", "0", CVAR_ARCHIVE};
cvar_t	r_fullbright = {"r_fullbright", "0", CVAR_NONE};
cvar_t	gl_fullbrights = { "gl_fullbrights", "1", CVAR_ARCHIVE };
cvar_t	r_lightmap = {"r_lightmap", "0", CVAR_NONE};
cvar_t	r_shadows = {"r_shadows", "0", CVAR_ARCHIVE};
cvar_t	r_mirroralpha = {"r_mirroralpha", "1", CVAR_NONE};
cvar_t	r_wateralpha = {"r_wateralpha", "0.33", CVAR_ARCHIVE};
cvar_t	r_skyalpha = {"r_skyalpha", "0.67", CVAR_ARCHIVE};
cvar_t	r_dynamic = {"r_dynamic", "1", CVAR_NONE};
cvar_t	r_novis = {"r_novis", "0", CVAR_NONE};
cvar_t	r_wholeframe = {"r_wholeframe", "1", CVAR_ARCHIVE};
cvar_t	r_texture_external = {"r_texture_external", "0", CVAR_ARCHIVE};

cvar_t	gl_clear = {"gl_clear", "0", CVAR_NONE};
cvar_t	gl_cull = {"gl_cull", "1", CVAR_NONE};
cvar_t	gl_ztrick = {"gl_ztrick", "0", CVAR_ARCHIVE};
cvar_t	gl_zfix = {"gl_zfix", "1", CVAR_ARCHIVE};
cvar_t	gl_smoothmodels = {"gl_smoothmodels", "1", CVAR_NONE};
cvar_t	gl_affinemodels = {"gl_affinemodels", "0", CVAR_NONE};
cvar_t	gl_polyblend = {"gl_polyblend", "1", CVAR_NONE};
cvar_t	gl_flashblend = {"gl_flashblend", "0", CVAR_NONE};
cvar_t	gl_playermip = {"gl_playermip", "0", CVAR_NONE};
cvar_t	gl_nocolors = {"gl_nocolors", "0", CVAR_NONE};
cvar_t	gl_keeptjunctions = {"gl_keeptjunctions", "1", CVAR_ARCHIVE};
cvar_t	gl_reporttjunctions = {"gl_reporttjunctions", "0", CVAR_NONE};
cvar_t	gl_waterripple = {"gl_waterripple", "2", CVAR_ARCHIVE};
cvar_t	gl_glows = {"gl_glows", "0", CVAR_ARCHIVE};	// torch glows
cvar_t	gl_other_glows = {"gl_other_glows", "0", CVAR_ARCHIVE};
cvar_t	gl_missile_glows = {"gl_missile_glows", "1", CVAR_ARCHIVE};
cvar_t	gl_overbright = { "gl_overbright", "1", CVAR_ARCHIVE };

cvar_t	gl_coloredlight = {"gl_coloredlight", "0", CVAR_ARCHIVE};
cvar_t	gl_colored_dynamic_lights = {"gl_colored_dynamic_lights", "0", CVAR_ARCHIVE};
cvar_t	gl_extra_dynamic_lights = {"gl_extra_dynamic_lights", "0", CVAR_ARCHIVE};

cvar_t    r_lerpmodels = { "r_lerpmodels", "1", CVAR_NONE };
cvar_t    gl_overbright_models = { "gl_overbright_models", "1", CVAR_ARCHIVE };
qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe, r_drawworld_cheatsafe; //johnfitz
qboolean gl_texture_env_add = false; //johnfitz
//=============================================================================


/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox (vec3_t mins, vec3_t maxs)
{
	int		i;

	for (i = 0; i < 4; i++)
	{
		if (BoxOnPlaneSide (mins, maxs, &frustum[i]) == 2)
			return true;
	}
	return false;
}


/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity(entity_t *e)
{
	vec3_t mins, maxs;

	if (e->angles[0] || e->angles[2]) //pitch or roll
	{
		VectorAdd(e->origin, e->model->rmins, mins);
		VectorAdd(e->origin, e->model->rmaxs, maxs);
	}
	else if (e->angles[1]) //yaw
	{
		VectorAdd(e->origin, e->model->ymins, mins);
		VectorAdd(e->origin, e->model->ymaxs, maxs);
	}
	else //no rotation
	{
		VectorAdd(e->origin, e->model->mins, mins);
		VectorAdd(e->origin, e->model->maxs, maxs);
	}

	return R_CullBox(mins, maxs);
}

/*
=================
R_RotateForEntity
=================
*/
void R_RotateForEntity (entity_t *e)
{
	glTranslatef_fp (e->origin[0], e->origin[1], e->origin[2]);

	glRotatef_fp (e->angles[1], 0, 0, 1);
	glRotatef_fp (-e->angles[0], 0, 1, 0);
	glRotatef_fp (-e->angles[2], 1, 0, 0);
}

/*
=================
R_RotateForEntity2

Same as R_RotateForEntity(), but checks for
EF_ROTATE and modifies yaw appropriately.
=================
*/
static void R_RotateForEntity2 (entity_t *e)
{
	float	forward, yaw, pitch;
	vec3_t			angles;

	glTranslatef_fp(e->origin[0], e->origin[1], e->origin[2]);

	if (e->model->flags & EF_FACE_VIEW)
	{
		VectorSubtract(e->origin,r_origin,angles);
		VectorSubtract(r_origin,e->origin,angles);
		VectorNormalize(angles);

		if (angles[1] == 0 && angles[0] == 0)
		{
			yaw = 0;
			if (angles[2] > 0)
				pitch = 90;
			else
				pitch = 270;
		}
		else
		{
			yaw = (int) (atan2(angles[1], angles[0]) * 180 / M_PI);
			if (yaw < 0)
				yaw += 360;

			forward = sqrt (angles[0]*angles[0] + angles[1]*angles[1]);
			pitch = (int) (atan2(angles[2], forward) * 180 / M_PI);
			if (pitch < 0)
				pitch += 360;
		}

		angles[0] = pitch;
		angles[1] = yaw;
		angles[2] = 0;

		glRotatef_fp (-angles[0], 0, 1, 0);
		glRotatef_fp (angles[1], 0, 0, 1);
//		glRotatef_fp (-angles[2], 1, 0, 0);
		glRotatef_fp (-e->angles[2], 1, 0, 0);
	}
	else
	{
		if (e->model->flags & EF_ROTATE)
		{
			glRotatef_fp (anglemod((e->origin[0] + e->origin[1])*0.8
								+ (108*cl.time)),
						    0, 0, 1);
		}
		else
		{
			glRotatef_fp (e->angles[1], 0, 0, 1);
		}

		glRotatef_fp (-e->angles[0], 0, 1, 0);
		glRotatef_fp (-e->angles[2], 1, 0, 0);
	}
}

/*
=============================================================

SPRITE MODELS

=============================================================
*/

/*
================
R_GetSpriteFrame
================
*/
static mspriteframe_t *R_GetSpriteFrame (entity_t *e)
{
	msprite_t	*psprite;
	mspritegroup_t	*pspritegroup;
	mspriteframe_t	*pspriteframe;
	int			i, numframes, frame;
	float		*pintervals, fullinterval, targettime, time;

	psprite = (msprite_t *) e->model->cache.data;
	frame = e->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_DPrintf ("%s: no such frame %d\n", __thisfunc__, frame);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes-1];

		time = cl.time + e->syncbase;

	// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
	// are positive, so we don't have to worry about division by 0
		targettime = time - ((int)(time / fullinterval)) * fullinterval;

		for (i = 0; i < (numframes-1); i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}


/*
=================
R_DrawSpriteModel

=================
*/
typedef struct
{
	vec3_t		vup, vright, vpn;	// in worldspace
} spritedesc_t;

static void R_DrawSpriteModel (entity_t *e)
{
	vec3_t		point;
	mspriteframe_t	*frame;
	msprite_t	*psprite;
	vec3_t		tvec;
	float		dot, angle, sr, cr;
	spritedesc_t	r_spritedesc;
	int			i;

	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t *) e->model->cache.data;

	if (psprite->type == SPR_FACING_UPRIGHT)
	{
	// generate the sprite's axes, with vup straight up in worldspace, and
	// r_spritedesc.vright perpendicular to modelorg.
	// This will not work if the view direction is very close to straight up or
	// down, because the cross product will be between two nearly parallel
	// vectors and starts to approach an undefined state, so we don't draw if
	// the two vectors are less than 1 degree apart
		tvec[0] = -modelorg[0];
		tvec[1] = -modelorg[1];
		tvec[2] = -modelorg[2];
		VectorNormalize (tvec);
		dot = tvec[2];	// same as DotProduct (tvec, r_spritedesc.vup)
				// because r_spritedesc.vup is 0, 0, 1

		if ((dot > 0.999848) || (dot < -0.999848))	// cos(1 degree) = 0.999848
			return;

		r_spritedesc.vup[0] = 0;
		r_spritedesc.vup[1] = 0;
		r_spritedesc.vup[2] = 1;
		r_spritedesc.vright[0] = tvec[1];
								// CrossProduct (r_spritedesc.vup, -modelorg,
		r_spritedesc.vright[1] = -tvec[0];
								//		 r_spritedesc.vright)
		r_spritedesc.vright[2] = 0;
		VectorNormalize (r_spritedesc.vright);
		r_spritedesc.vpn[0] = -r_spritedesc.vright[1];
		r_spritedesc.vpn[1] = r_spritedesc.vright[0];
		r_spritedesc.vpn[2] = 0;
					// CrossProduct (r_spritedesc.vright, r_spritedesc.vup,
					//		 r_spritedesc.vpn)
	}
	else if (psprite->type == SPR_VP_PARALLEL)
	{
	// generate the sprite's axes, completely parallel to the viewplane. There
	// are no problem situations, because the sprite is always in the same
	// position relative to the viewer
		for (i = 0; i < 3; i++)
		{
			r_spritedesc.vup[i] = vup[i];
			r_spritedesc.vright[i] = vright[i];
			r_spritedesc.vpn[i] = vpn[i];
		}
	}
	else if (psprite->type == SPR_VP_PARALLEL_UPRIGHT)
	{
	// generate the sprite's axes, with vup straight up in worldspace, and
	// r_spritedesc.vright parallel to the viewplane.
	// This will not work if the view direction is very close to straight up or
	// down, because the cross product will be between two nearly parallel
	// vectors and starts to approach an undefined state, so we don't draw if
	// the two vectors are less than 1 degree apart
		dot = vpn[2];	// same as DotProduct (vpn, r_spritedesc.vup)
				// because r_spritedesc.vup is 0, 0, 1

		if ((dot > 0.999848) || (dot < -0.999848))	// cos(1 degree) = 0.999848
			return;

		r_spritedesc.vup[0] = 0;
		r_spritedesc.vup[1] = 0;
		r_spritedesc.vup[2] = 1;
		r_spritedesc.vright[0] = vpn[1];
							// CrossProduct (r_spritedesc.vup, vpn,
		r_spritedesc.vright[1] = -vpn[0];	//		 r_spritedesc.vright)
		r_spritedesc.vright[2] = 0;
		VectorNormalize (r_spritedesc.vright);
		r_spritedesc.vpn[0] = -r_spritedesc.vright[1];
		r_spritedesc.vpn[1] = r_spritedesc.vright[0];
		r_spritedesc.vpn[2] = 0;
					// CrossProduct (r_spritedesc.vright, r_spritedesc.vup,
					//		 r_spritedesc.vpn)
	}
	else if (psprite->type == SPR_ORIENTED)
	{
	// generate the sprite's axes, according to the sprite's world orientation
		AngleVectors (e->angles, r_spritedesc.vpn, r_spritedesc.vright, r_spritedesc.vup);
	}
	else if (psprite->type == SPR_VP_PARALLEL_ORIENTED)
	{
	// generate the sprite's axes, parallel to the viewplane, but rotated in
	// that plane around the center according to the sprite entity's roll
	// angle. So vpn stays the same, but vright and vup rotate
		angle = e->angles[ROLL] * (M_PI*2 / 360);
		sr = sin(angle);
		cr = cos(angle);

		for (i = 0; i < 3; i++)
		{
			r_spritedesc.vpn[i] = vpn[i];
			r_spritedesc.vright[i] = vright[i] * cr + vup[i] * sr;
			r_spritedesc.vup[i] = vright[i] * -sr + vup[i] * cr;
		}
	}
	else
	{
		Sys_Error ("%s: Bad sprite type %d", __thisfunc__, psprite->type);
	}

/* Pa3PyX: new translucency code below
	if (e->drawflags & DRF_TRANSLUCENT)
	{
		glEnable_fp (GL_BLEND);
		glColor4f_fp (1,1,1,r_wateralpha.value);
	}
	else if (e->model->flags & EF_TRANSPARENT)
	{
		glEnable_fp (GL_BLEND);
		glColor3f_fp (1,1,1);
	}
	else
	{
		glEnable_fp (GL_BLEND);
		glColor3f_fp (1,1,1);
	}
*/
	/* Pa3PyX: new translucency mechanism (doesn't look
	   as good, should work with non 3Dfx MiniGL drivers */
	if ((e->drawflags & DRF_TRANSLUCENT) || (e->model->flags & EF_TRANSPARENT))
	{
		glDisable_fp (GL_ALPHA_TEST);
		glEnable_fp (GL_BLEND);
		glTexEnvf_fp (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f_fp (1.0f, 1.0f, 1.0f, r_wateralpha.value);
	}
	else
	{
	/* pa3pyx's alpha code looks rather ugly, use the original one.
		glDisable_fp (GL_BLEND);
		glEnable_fp (GL_ALPHA_TEST);
		glAlphaFunc_fp (GL_GREATER, 0.632);
		glColor4f_fp (1.0f, 1.0f, 1.0f, 1.0f);
	*/
	/* here, we use the original alpha code	*/
		glEnable_fp (GL_BLEND);
		glColor3f_fp (1,1,1);
	}

	GL_Bind(frame->gl_texturenum);

	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

	glBegin_fp (GL_QUADS);

	glTexCoord2f_fp (0, 1);
	VectorMA (e->origin, frame->down, r_spritedesc.vup, point);
	VectorMA (point, frame->left, r_spritedesc.vright, point);
	glVertex3fv_fp (point);

	glTexCoord2f_fp (0, 0);
	VectorMA (e->origin, frame->up, r_spritedesc.vup, point);
	VectorMA (point, frame->left, r_spritedesc.vright, point);
	glVertex3fv_fp (point);

	glTexCoord2f_fp (1, 0);
	VectorMA (e->origin, frame->up, r_spritedesc.vup, point);
	VectorMA (point, frame->right, r_spritedesc.vright, point);
	glVertex3fv_fp (point);

	glTexCoord2f_fp (1, 1);
	VectorMA (e->origin, frame->down, r_spritedesc.vup, point);
	VectorMA (point, frame->right, r_spritedesc.vright, point);
	glVertex3fv_fp (point);

	glEnd_fp ();

// restore tex parms
	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameterf_fp(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

/* for pa3pyx's translucency code changes above
	glDisable_fp (GL_BLEND);
*/
	if ((e->drawflags & DRF_TRANSLUCENT) || (e->model->flags & EF_TRANSPARENT))
	{
		glDisable_fp (GL_BLEND);
		glTexEnvf_fp (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}
	else
	{
	/* not using pa3pyx's alpha code (see above)
		glDisable_fp (GL_ALPHA_TEST);
	*/
		glDisable_fp (GL_BLEND);
	}
}


/*
=============================================================

ALIAS MODELS

=============================================================
*/

static vec3_t	shadevector;
static float	shadelight, ambientlight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT		16
static float	r_avertexnormal_dots[SHADEDOT_QUANT][256] =
{
#include "anorm_dots.h"
};

static float	*shadedots = r_avertexnormal_dots[0];

static int	lastposenum;

/*
=============
GL_DrawAliasFrame -- johnfitz -- rewritten to support colored light, lerping, entalpha, multitexture, and r_drawflat
=============
*/
void GL_DrawAliasFrame(aliashdr_t *paliashdr, lerpdata_t lerpdata)
{
	float	vertcolor[4];
	trivertx_t *verts1, *verts2;
	int		*commands;
	int		count;
	float	u, v;
	float	blend, iblend;
	qboolean lerping;

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		lerping = true;
		verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
		verts2 = verts1;
		verts1 += lerpdata.pose1 * paliashdr->poseverts;
		verts2 += lerpdata.pose2 * paliashdr->poseverts;
		blend = lerpdata.blend;
		iblend = 1.0f - blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		lerping = false;
		verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
		verts2 = verts1; // avoid bogus compiler warning
		verts1 += lerpdata.pose1 * paliashdr->poseverts;
		blend = iblend = 0; // avoid bogus compiler warning
	}

	commands = (int *)((byte *)paliashdr + paliashdr->commands);

	vertcolor[3] = entalpha; //never changes, so there's no need to put this inside the loop

	while (1)
	{
		// get the vertex count and primitive type
		count = *commands++;
		if (!count)
			break;		// done

		if (count < 0)
		{
			count = -count;
			glBegin_fp(GL_TRIANGLE_FAN);
		}
		else
			glBegin_fp(GL_TRIANGLE_STRIP);

		do
		{
			u = ((float *)commands)[0];
			v = ((float *)commands)[1];
			if (mtexenabled)
			{
				GL_MTexCoord2fFunc(GL_TEXTURE0_ARB, u, v);
				GL_MTexCoord2fFunc(GL_TEXTURE1_ARB, u, v);
			}
			else
				glTexCoord2f_fp(u, v);

			commands += 2;

			if (shading)
			{
				if (r_drawflat_cheatsafe)
				{
					srand(count * (unsigned int)(src_offset_t)commands);
					glColor3f_fp(rand() % 256 / 255.0, rand() % 256 / 255.0, rand() % 256 / 255.0);
				}
				else if (lerping)
				{
					vertcolor[0] = (shadedots[verts1->lightnormalindex] * iblend + shadedots[verts2->lightnormalindex] * blend) * lightcolor[0];
					vertcolor[1] = (shadedots[verts1->lightnormalindex] * iblend + shadedots[verts2->lightnormalindex] * blend) * lightcolor[1];
					vertcolor[2] = (shadedots[verts1->lightnormalindex] * iblend + shadedots[verts2->lightnormalindex] * blend) * lightcolor[2];
					glColor4fv_fp(vertcolor);
				}
				else
				{
					vertcolor[0] = shadedots[verts1->lightnormalindex] * lightcolor[0];
					vertcolor[1] = shadedots[verts1->lightnormalindex] * lightcolor[1];
					vertcolor[2] = shadedots[verts1->lightnormalindex] * lightcolor[2];
					glColor4fv_fp(vertcolor);
				}
			}

			if (lerping)
			{
				glVertex3f_fp(verts1->v[0] * iblend + verts2->v[0] * blend,
					verts1->v[1] * iblend + verts2->v[1] * blend,
					verts1->v[2] * iblend + verts2->v[2] * blend);
				verts1++;
				verts2++;
			}
			else
			{
				glVertex3f_fp(verts1->v[0], verts1->v[1], verts1->v[2]);
				verts1++;
			}
		} while (--count);

		glEnd_fp();
	}

	rs_aliaspasses += paliashdr->numtris;
}


/*
=============
GL_DrawAliasShadow
=============
*/
static void GL_DrawAliasShadow (entity_t *e, aliashdr_t *paliashdr, int posenum)
{
	trivertx_t	*verts;
	int		*order;
	vec3_t		point;
	float		height, lheight;
	int		count;

	lheight = e->origin[2] - lightspot[2];

	height = 0;
	verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
	verts += posenum * paliashdr->poseverts;
	order = (int *)((byte *)paliashdr + paliashdr->commands);

	height = -lheight + 1.0;

	if (have_stencil)
	{
		glEnable_fp(GL_STENCIL_TEST);
		glStencilFunc_fp(GL_EQUAL,1,2);
		glStencilOp_fp(GL_KEEP,GL_KEEP,GL_INCR);
	}

	while (1)
	{
		// get the vertex count and primitive type
		count = *order++;
		if (!count)
			break;		// done
		if (count < 0)
		{
			count = -count;
			glBegin_fp (GL_TRIANGLE_FAN);
		}
		else
			glBegin_fp (GL_TRIANGLE_STRIP);

		do
		{
			// texture coordinates come from the draw list
			// (skipped for shadows) glTexCoord2fv_fp ((float *)order);
			order += 2;

			// normals and vertexes come from the frame list
			point[0] = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
			point[1] = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
			point[2] = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];

			point[0] -= shadevector[0]*(point[2]+lheight);
			point[1] -= shadevector[1]*(point[2]+lheight);
			point[2] = height;
//			height -= 0.001;
			glVertex3fv_fp (point);

			verts++;
		} while (--count);

		glEnd_fp ();
	}

	if (have_stencil)
		glDisable_fp(GL_STENCIL_TEST);
}


/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame(aliashdr_t *paliashdr, int frame, lerpdata_t *lerpdata)
{
	entity_t		*e = currententity;
	int				posenum, numposes;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	posenum = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		e->lerptime = paliashdr->frames[frame].interval;
		posenum += (int)(cl.time / e->lerptime) % numposes;
	}
	else
		e->lerptime = 0.1;

	if (e->lerpflags & LERP_RESETANIM) //kill any lerp in progress
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags -= LERP_RESETANIM;
	}
	else if (e->currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) //defer lerping one more time
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	//set up values
	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		if (e->lerpflags & LERP_FINISH && numposes == 1)
			lerpdata->blend = CLAMP(0, (cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1);
		else
			lerpdata->blend = CLAMP(0, (cl.time - e->lerpstart) / e->lerptime, 1);
		lerpdata->pose1 = e->previouspose;
		lerpdata->pose2 = e->currentpose;
	}
	else //don't lerp
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = posenum;
		lerpdata->pose2 = posenum;
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting(entity_t	*e)
{
	vec3_t		dist;
	float		add;
	int			i;
	int		quantizedangle;
	float		radiansangle;

	R_LightPoint(e->origin);

	//add dlights
	for (i = 0; i < MAX_DLIGHTS; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract(currententity->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - VectorLength(dist);
			if (add > 0)
				VectorMA(lightcolor, add, cl_dlights[i].color, lightcolor);
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (currententity > cl_entities && currententity <= cl_entities + cl.maxclients)
	{
		add = 24.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	if (overbright)
	{
		add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add < 1.0f)
			VectorScale(lightcolor, add, lightcolor);
	}

	//hack up the brightness when fullbrights but no overbrights (256)
	if (gl_fullbrights.value && !gl_overbright_models.value)
		if (e->model->flags & MOD_FBRIGHTHACK)
		{
			lightcolor[0] = 256.0f;
			lightcolor[1] = 256.0f;
			lightcolor[2] = 256.0f;
		}

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

	//ericw -- shadevector is passed to the shader to compute shadedots inside the
	//shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	shadevector[0] = cos(-radiansangle);
	shadevector[1] = sin(-radiansangle);
	shadevector[2] = 1;
	VectorNormalize(shadevector);
	//ericw --

	shadedots = r_avertexnormal_dots[quantizedangle];
	VectorScale(lightcolor, 1.0f / 200.0f, lightcolor);
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel(entity_t *e)
{
	aliashdr_t	*paliashdr;
	int			i, anim, skinnum;
	gltexture_t	*tx, *fb;
	lerpdata_t	lerpdata;
	qboolean	alphatest = !!(e->model->flags & EF_HOLEY);

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata(e->model);
	R_SetupAliasFrame(paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform(e, &lerpdata);

	//
	// cull it
	//
	if (R_CullModelForEntity(e))
		return;

	//
	// transform it
	//
	glPushMatrix_fp();
	R_RotateForEntity(lerpdata.origin, lerpdata.angles);
	glTranslatef_fp(paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef_fp(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

	//
	// random stuff
	//
	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
		glShadeModel_fp(GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint_fp(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	overbright = gl_overbright_models.value;
	shading = true;

	//
	// set up for alpha blending
	//
	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe) //no alpha in drawflat or lightmap mode
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0)
		goto cleanup;
	if (entalpha < 1)
	{
		if (!gl_texture_env_combine) overbright = false; //overbright can't be done in a single pass without combiners
		glDepthMask_fp(GL_FALSE);
		glEnable_fp(GL_BLEND);
	}
	else if (alphatest)
		glEnable_fp(GL_ALPHA_TEST);

	//
	// set up lighting
	//
	rs_aliaspolys += paliashdr->numtris;
	R_SetupAliasLighting(e);

	//
	// set up textures
	//
	GL_DisableMultitexture();
	anim = (int)(cl.time * 10) & 3;
	skinnum = e->skinnum;
	if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
	{
		Con_DPrintf("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
		// ericw -- display skin 0 for winquake compatibility
		skinnum = 0;
	}
	tx = paliashdr->gltextures[skinnum][anim];
	fb = paliashdr->fbtextures[skinnum][anim];
	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = e - cl_entities;
		if (i >= 1 && i <= cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
			tx = playertextures[i - 1];
	}
	if (!gl_fullbrights.value)
		fb = NULL;

	//
	// draw it
	//
	if (r_drawflat_cheatsafe)
	{
		glDisable_fp(GL_TEXTURE_2D);
		GL_DrawAliasFrame(paliashdr, lerpdata);
		glEnable_fp(GL_TEXTURE_2D);
		srand((int)(cl.time * 1000)); //restore randomness
	}
	else if (r_fullbright_cheatsafe)
	{
		GL_Bind(tx);
		shading = false;
		glColor4f_fp(1, 1, 1, entalpha);
		GL_DrawAliasFrame(paliashdr, lerpdata);
		if (fb)
		{
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_Bind(fb);
			glEnable_fp(GL_BLEND);
			glBlendFunc_fp(GL_ONE, GL_ONE);
			glDepthMask_fp(GL_FALSE);
			glColor3f_fp(entalpha, entalpha, entalpha);
			Fog_StartAdditive();
			GL_DrawAliasFrame(paliashdr, lerpdata);
			Fog_StopAdditive();
			glDepthMask_fp(GL_TRUE);
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable_fp(GL_BLEND);
		}
	}
	else if (r_lightmap_cheatsafe)
	{
		glDisable_fp(GL_TEXTURE_2D);
		shading = false;
		glColor3f_fp(1, 1, 1);
		GL_DrawAliasFrame(paliashdr, lerpdata);
		glEnable_fp(GL_TEXTURE_2D);
	}
	// call fast path if possible. if the shader compliation failed for some reason,
	// r_alias_program will be 0.
	else if (r_alias_program != 0)
	{
		GL_DrawAliasFrame_GLSL(paliashdr, lerpdata, tx, fb);
	}
	else if (overbright)
	{
		if (gl_texture_env_combine && gl_mtexable && gl_texture_env_add && fb) //case 1: everything in one pass
		{
			GL_Bind(tx);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind(fb);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			glEnable_fp(GL_BLEND);
			GL_DrawAliasFrame(paliashdr, lerpdata);
			glDisable_fp(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (gl_texture_env_combine) //case 2: overbright in one pass, then fullbright pass
		{
			// first pass
			GL_Bind(tx);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
			glTexEnvi_fp(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_DrawAliasFrame(paliashdr, lerpdata);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			// second pass
			if (fb)
			{
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_Bind(fb);
				glEnable_fp(GL_BLEND);
				glBlendFunc_fp(GL_ONE, GL_ONE);
				glDepthMask_fp(GL_FALSE);
				shading = false;
				glColor3f_fp(entalpha, entalpha, entalpha);
				Fog_StartAdditive();
				GL_DrawAliasFrame(paliashdr, lerpdata);
				Fog_StopAdditive();
				glDepthMask_fp(GL_TRUE);
				glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable_fp(GL_BLEND);
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
		}
		else //case 3: overbright in two passes, then fullbright pass
		{
			// first pass
			GL_Bind(tx);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DrawAliasFrame(paliashdr, lerpdata);
			// second pass -- additive with black fog, to double the object colors but not the fog color
			glEnable_fp(GL_BLEND);
			glBlendFunc_fp(GL_ONE, GL_ONE);
			glDepthMask_fp(GL_FALSE);
			Fog_StartAdditive();
			GL_DrawAliasFrame(paliashdr, lerpdata);
			Fog_StopAdditive();
			glDepthMask_fp(GL_TRUE);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable_fp(GL_BLEND);
			// third pass
			if (fb)
			{
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_Bind(fb);
				glEnable_fp(GL_BLEND);
				glBlendFunc_fp(GL_ONE, GL_ONE);
				glDepthMask_fp(GL_FALSE);
				shading = false;
				glColor3f_fp(entalpha, entalpha, entalpha);
				Fog_StartAdditive();
				GL_DrawAliasFrame(paliashdr, lerpdata);
				Fog_StopAdditive();
				glDepthMask_fp(GL_TRUE);
				glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable_fp(GL_BLEND);
				glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
		}
	}
	else
	{
		if (gl_mtexable && gl_texture_env_add && fb) //case 4: fullbright mask using multitexture
		{
			GL_DisableMultitexture(); // selects TEXTURE0
			GL_Bind(tx);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind(fb);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			glEnable_fp(GL_BLEND);
			GL_DrawAliasFrame(paliashdr, lerpdata);
			glDisable_fp(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else //case 5: fullbright mask without multitexture
		{
			// first pass
			GL_Bind(tx);
			glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DrawAliasFrame(paliashdr, lerpdata);
			// second pass
			if (fb)
			{
				GL_Bind(fb);
				glEnable_fp(GL_BLEND);
				glBlendFunc_fp(GL_ONE, GL_ONE);
				glDepthMask_fp(GL_FALSE);
				shading = false;
				glColor3f_fp(entalpha, entalpha, entalpha);
				Fog_StartAdditive();
				GL_DrawAliasFrame(paliashdr, lerpdata);
				Fog_StopAdditive();
				glDepthMask_fp(GL_TRUE);
				glBlendFunc_fp(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable_fp(GL_BLEND);
			}
		}
	}

cleanup:
	glTexEnvf_fp(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glHint_fp(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glShadeModel_fp(GL_FLAT);
	glDepthMask_fp(GL_TRUE);
	glDisable_fp(GL_BLEND);
	if (alphatest)
		glDisable_fp(GL_ALPHA_TEST);
	glColor3f_fp(1, 1, 1);
	glPopMatrix_fp();
}

//=============================================================================

typedef struct sortedent_s {
	entity_t	*ent;
	vec_t		len;
} sortedent_t;

static sortedent_t	cl_transvisedicts[MAX_VISEDICTS];
static sortedent_t	cl_transwateredicts[MAX_VISEDICTS];

static int			cl_numtransvisedicts;
static int			cl_numtranswateredicts;

/*
=============
R_DrawEntitiesOnList
=============
*/
static void R_DrawEntitiesOnList (void)
{
	int			i;
	qboolean	item_trans;
	mleaf_t		*pLeaf;
	entity_t	*e;

	cl_numtransvisedicts = 0;
	cl_numtranswateredicts = 0;

	if (!r_drawentities.integer)
		return;

	// draw sprites seperately, because of alpha blending
	for (i = 0; i < cl_numvisedicts; i++)
	{
		e = cl_visedicts[i];

		// chase-cam pitch adj. by FrikaC
		if (e == &cl_entities[cl.viewentity])
			e->angles[0] *= 0.3;

		switch (e->model->type)
		{
		case mod_alias:
			item_trans = ((e->drawflags & DRF_TRANSLUCENT) ||
					(e->model->flags & (EF_TRANSPARENT|EF_HOLEY|EF_SPECIAL_TRANS))) != 0;
			if (!item_trans)
				R_DrawAliasModel (e);
			break;

		case mod_brush:
			item_trans = ((e->drawflags & DRF_TRANSLUCENT)) != 0;
			if (!item_trans)
				R_DrawBrushModel (e,false);
			break;

		case mod_sprite:
			item_trans = true;
			break;

		default:
			item_trans = false;
			break;
		}

		if (item_trans)
		{
			pLeaf = Mod_PointInLeaf (e->origin, cl.worldmodel);
		//	if (pLeaf->contents == CONTENTS_EMPTY)
			if (pLeaf->contents != CONTENTS_WATER)
				cl_transvisedicts[cl_numtransvisedicts++].ent = e;
			else
				cl_transwateredicts[cl_numtranswateredicts++].ent = e;
		}
	}
}


/*
================
R_DrawTransEntitiesOnList
Implemented by: jack
================
*/

static int transCompare (const void *arg1, const void *arg2)
{
	const sortedent_t *a1, *a2;
	a1 = (sortedent_t *) arg1;
	a2 = (sortedent_t *) arg2;
	return (a2->len - a1->len); // Sorted in reverse order.  Neat, huh?
}

static void R_DrawTransEntitiesOnList (qboolean inwater)
{
	int		i;
	int		numents;
	sortedent_t	*theents;
	entity_t	*e;
	int	depthMaskWrite = 0;
	vec3_t	result;

	theents = (inwater) ? cl_transwateredicts : cl_transvisedicts;
	numents = (inwater) ? cl_numtranswateredicts : cl_numtransvisedicts;

	for (i = 0; i < numents; i++)
	{
		VectorSubtract(theents[i].ent->origin, r_origin, result);
	//	theents[i].len = VectorLength(result);
		theents[i].len = (result[0] * result[0]) + (result[1] * result[1]) + (result[2] * result[2]);
	}

	qsort((void *) theents, numents, sizeof(sortedent_t), transCompare);
	// Add in BETTER sorting here

	glDepthMask_fp(0);
	for (i = 0; i < numents; i++)
	{
		e = theents[i].ent;

		switch (e->model->type)
		{
		case mod_alias:
			if (!depthMaskWrite)
			{
				depthMaskWrite = 1;
				glDepthMask_fp(1);
			}
			R_DrawAliasModel (e);
			break;
		case mod_brush:
			if (!depthMaskWrite)
			{
				depthMaskWrite = 1;
				glDepthMask_fp(1);
			}
			R_DrawBrushModel (e, true);
			break;
		case mod_sprite:
			if (depthMaskWrite)
			{
				depthMaskWrite = 0;
				glDepthMask_fp(0);
			}
			R_DrawSpriteModel (e);
			break;
		}
	}

	if (!depthMaskWrite)
		glDepthMask_fp(1);
}

//=============================================================================


// Glow styles. These rely on unchanged game code!
#define	TORCH_STYLE	1	/* Flicker	*/
#define	MISSILE_STYLE	6	/* Flicker	*/
#define	PULSE_STYLE	11	/* Slow pulse	*/

static void R_DrawGlow (entity_t *e)
{
	qmodel_t	*clmodel;

	clmodel = e->model;

	// Torches & Flames
	if ((gl_glows.integer && (clmodel->ex_flags & XF_TORCH_GLOW)) ||
	    (gl_missile_glows.integer && (clmodel->ex_flags & XF_MISSILE_GLOW)) ||
	    (gl_other_glows.integer && (clmodel->ex_flags & XF_GLOW)) )
	{
		// NOTE: It would be better if we batched these up.
		//	 All those state changes are not nice. KH
		vec3_t	lightorigin;		// Origin of torch.
		vec3_t	glow_vect;		// Vector to torch.
		float	radius;			// Radius of torch flare.
		float	distance;		// Vector distance to torch.
		float	intensity;		// Intensity of torch flare.
		int	i, j;
		vec3_t	vp2;

		// NOTE: I don't think this is centered on the model.
		VectorCopy(e->origin, lightorigin);

		radius = 20.0f;

		// for mana, make it bit bigger
		if ( !q_strncasecmp(clmodel->name, "models/i_btmana", 15))
			radius += 5.0f;

		VectorSubtract(lightorigin, r_origin, vp2);

		// See if view is outside the light.
		distance = VectorLength(vp2);

		if (distance > radius)
		{
			VectorNormalize(vp2);
			glPushMatrix_fp();

			// Translate the glow to coincide with the flame. KH
			if (clmodel->ex_flags & XF_TORCH_GLOW)
			{
				if (clmodel->ex_flags & XF_TORCH_GLOW_EGYPT)	// egypt torch fix
					glTranslatef_fp (cos(e->angles[1]/180*M_PI)*8.0f, sin(e->angles[1]/180*M_PI)*8.0f, 16.0f);
				else	glTranslatef_fp (0.0f, 0.0f, 8.0f);
			}

			// 'floating' movement
			if (clmodel->flags & EF_ROTATE)
				glTranslatef_fp (0, 0, sin(e->origin[0] + e->origin[1] + (cl.time*3))*5.5);

			glBegin_fp(GL_TRIANGLE_FAN);
			// Diminish torch flare inversely with distance.
			intensity = (1024.0f - distance) / 1024.0f;

			// Invert (fades as you approach).
			intensity = (1.0f - intensity);

			// Clamp, but don't let the flare disappear.
			if (intensity > 1.0f)
				intensity = 1.0f;
			else if (intensity < 0.0f)
				intensity = 0.0f;

			// Now modulate with flicker.
			j = 0;	// avoid compiler warning
			if (clmodel->ex_flags & XF_TORCH_GLOW)
			{
				i = (int)(cl.time*10);
				if (!cl_lightstyle[TORCH_STYLE].length)
				{
					j = 256;
				}
				else
				{
					j = i % cl_lightstyle[TORCH_STYLE].length;
					j = cl_lightstyle[TORCH_STYLE].map[j] - 'a';
					j = j * 22;
				}
			}
			else if (clmodel->ex_flags & XF_MISSILE_GLOW)
			{
				i = (int)(cl.time*10);
				if (!cl_lightstyle[MISSILE_STYLE].length)
				{
					j = 256;
				}
				else
				{
					j = i % cl_lightstyle[MISSILE_STYLE].length;
					j = cl_lightstyle[MISSILE_STYLE].map[j] - 'a';
					j = j * 22;
				}
			}
			else if (clmodel->ex_flags & XF_GLOW)
			{
				i = (int)(cl.time*10);
				if (!cl_lightstyle[PULSE_STYLE].length)
				{
					j = 256;
				}
				else
				{
					j = i % cl_lightstyle[PULSE_STYLE].length;
					j = cl_lightstyle[PULSE_STYLE].map[j] - 'a';
					j = j * 22;
				}
			}

			intensity *= ((float)j / 255.0f);
			glColor4f_fp (clmodel->glow_color[0]*intensity,
					clmodel->glow_color[1]*intensity,
					clmodel->glow_color[2]*intensity,
					clmodel->glow_color[3]);

			for (i = 0; i < 3; i++)
				glow_vect[i] = lightorigin[i] - vp2[i]*radius;

			glVertex3fv_fp(glow_vect);

			glColor4f_fp(0.0f, 0.0f, 0.0f, 1.0f);

			for (i = 16; i >= 0; i--)
			{
				float a = i / 16.0f * M_PI * 2;

				for (j = 0; j < 3; j++)
					glow_vect[j] = lightorigin[j] + vright[j]*cos(a)*radius + vup[j]*sin(a)*radius;

				glVertex3fv_fp(glow_vect);
			}

			glEnd_fp();
			glColor4f_fp (0.0f, 0.0f, 0.0f, 1.0f);
			// Restore previous matrix
			glPopMatrix_fp();
		}
	}
}

static void R_DrawAllGlows (void)
{
	int		i;
	entity_t	*e;

	if (!r_drawentities.integer)
		return;

	glDepthMask_fp (0);
	glDisable_fp (GL_TEXTURE_2D);
	glShadeModel_fp (GL_SMOOTH);
	glEnable_fp (GL_BLEND);
	glBlendFunc_fp (GL_ONE, GL_ONE);

	for (i = 0; i < cl_numvisedicts; i++)
	{
		e = cl_visedicts[i];

		switch (e->model->type)
		{
		case mod_alias:
			R_DrawGlow (e);
			break;
		default:
			break;
		}
	}

	glDisable_fp (GL_BLEND);
	glEnable_fp (GL_TEXTURE_2D);
	glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask_fp (1);
	glShadeModel_fp (GL_FLAT);
}

//=============================================================================


/*
=============
R_DrawViewModel
=============
*/
static void R_DrawViewModel (void)
{
	int			lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	entity_t	*e;

	e = &cl.viewent;

	if (!e->model)
		return;

	if (gl_lightmap_format == GL_RGBA)
	{
		ambientlight = R_LightPointColor (e->origin);
		if (lightcolor[0] < 24)
			lightcolor[0] = 24;
		if (lightcolor[1] < 24)
			lightcolor[1] = 24;
		if (lightcolor[2] < 24)
			lightcolor[2] = 24;
		if (ambientlight < 24)
			ambientlight = 24;		// always give some light on gun
	}
	else
	{
		ambientlight = shadelight = R_LightPoint (e->origin);
		if (ambientlight < 24)
			ambientlight = shadelight = 24;	// always give some light on gun
	}

// add dynamic lights
	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (dl->die < cl.time)
			continue;

		VectorSubtract (e->origin, dl->origin, dist);
		add = dl->radius - VectorLength(dist);
		if (add > 0)
		{
			if (gl_lightmap_format == GL_RGBA)
			{
				lightcolor[0] += (float) (dl->color[0] * add);
				lightcolor[1] += (float) (dl->color[1] * add);
				lightcolor[2] += (float) (dl->color[2] * add);
			}
			else
			{
				shadelight += (float) add;
			}

			ambientlight += add;
		}
	}

	cl.light_level = ambientlight;

	if ((cl.v.health <= 0) ||
	    (chase_active.integer) ||
//rjr	    (cl.items & IT_INVISIBILITY) ||
	    (!r_drawviewmodel.integer) ||
	    (!r_drawentities.integer))
	{
		return;
	}

	// hack the depth range to prevent view model from poking into walls
	glDepthRange_fp (gldepthmin, gldepthmin + 0.3*(gldepthmax-gldepthmin));
	AlwaysDrawModel = true;
	R_DrawAliasModel (e);
	AlwaysDrawModel = false;
	glDepthRange_fp (gldepthmin, gldepthmax);
}

//=============================================================================


/*
===============
R_MarkLeaves
===============
*/
static void R_MarkLeaves (void)
{
	byte	*vis;
	mnode_t	*node;
	int		i;
	byte	solid[8192];

	if (r_oldviewleaf == r_viewleaf && !r_novis.integer)
		return;

	if (mirror)
		return;

	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	if (r_novis.integer)
	{
		vis = solid;
		memset (solid, 0xff, (cl.worldmodel->numleafs+7)>>3);
	}
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	for (i = 0; i < cl.worldmodel->numleafs; i++)
	{
		if ( vis[i>>3] & (1<<(i&7)) )
		{
			node = (mnode_t *)&cl.worldmodel->leafs[i+1];
			do
			{
				if (node->visframe == r_visframecount)
					break;
				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}

//=============================================================================


/*
=================
GL_DrawBlendPoly

Renders a polygon covering the whole screen. For
fullscreen color blending and approximated gamma
correction. To be called from R_PolyBlend().
=================
*/
static void GL_DrawBlendPoly (void)
{
	glBegin_fp (GL_QUADS);
	glVertex3f_fp (10, 100, 100);
	glVertex3f_fp (10, -100, 100);
	glVertex3f_fp (10, -100, -100);
	glVertex3f_fp (10, 100, -100);
	glEnd_fp ();
}

/*
=================
GL_DoGamma

Uses GL_DrawBlendPoly() for gamma correction.
Idea originally from LordHavoc.
This trick is useful if normal ways of gamma
adjustment fail: In case of 3dfx Voodoo1/2/Rush,
we can't use 3dfx specific extensions in unix,
so this can be our friend at a cost of 4-5 fps.
To be called from R_PolyBlend().
=================
*/
#if 0
static void GL_DoGamma (void)
{
	if (v_gamma.value >= 1)
		return;

	glBlendFunc_fp (GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f_fp (1, 1, 1, v_gamma.value);

	GL_DrawBlendPoly ();

	glBlendFunc_fp (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
#endif

/*
============
R_PolyBlend
============
*/
static void R_PolyBlend (void)
{
	if (!gl_polyblend.integer)
		return;

	glEnable_fp (GL_BLEND);
	glDisable_fp (GL_DEPTH_TEST);
	glDisable_fp (GL_TEXTURE_2D);

	glLoadIdentity_fp ();

	glRotatef_fp (-90,  1, 0, 0);	// put Z going up
	glRotatef_fp (90,  0, 0, 1);	// put Z going up

	if (v_blend[3])
	{
		glColor4fv_fp (v_blend);
		GL_DrawBlendPoly ();
	}

	/*GL_DoGamma ();*/

	glDisable_fp (GL_BLEND);
	glEnable_fp (GL_TEXTURE_2D);
	glEnable_fp (GL_ALPHA_TEST);
}

//=============================================================================


static int SignbitsForPlane (mplane_t *out)
{
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j = 0; j < 3; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1<<j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
static void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float	scale_forward, scale_side;

	scale_forward = cos(angle * M_PI / 180.0);
	scale_side = sin(angle * M_PI / 180.0);

	out[0] = scale_forward*forward[0] + scale_side*side[0];
	out[1] = scale_forward*forward[1] + scale_side*side[1];
	out[2] = scale_forward*forward[2] + scale_side*side[2];
}

static void R_SetFrustum (void)
{
	int		i;

	if (r_refdef.fov_x == 90)
	{
		// front side is visible
		VectorAdd (vpn, vright, frustum[0].normal);
		VectorSubtract (vpn, vright, frustum[1].normal);
		VectorAdd (vpn, vup, frustum[2].normal);
		VectorSubtract (vpn, vup, frustum[3].normal);
	}
	else
	{
		TurnVector(frustum[0].normal, vpn, vright, r_refdef.fov_x/2 - 90); // left plane
		TurnVector(frustum[1].normal, vpn, vright, 90 - r_refdef.fov_x/2); // right plane
		TurnVector(frustum[2].normal, vpn, vup,    90 - r_refdef.fov_y/2); // bottom plane
		TurnVector(frustum[3].normal, vpn, vup,    r_refdef.fov_y/2 - 90); // top plane
	}

	for (i = 0; i < 4; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal);
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}


/*
===============
R_SetupFrame
===============
*/
static void R_SetupFrame (void)
{
// don't allow cheats in multiplayer
	if (cl.maxclients > 1)
		Cvar_SetQuick (&r_fullbright, "0");

	R_AnimateLight ();

	r_framecount++;

// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);

	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	c_brush_polys = 0;
	c_alias_polys = 0;
}


#define NEARCLIP	4
#define FARCLIP		4096
static void GL_SetFrustum (GLdouble fovx, GLdouble fovy)
{
	GLdouble	xmax, ymax;
	xmax = NEARCLIP * tan(fovx * M_PI / 360.0);
	ymax = NEARCLIP * tan(fovy * M_PI / 360.0);
	glFrustum_fp (-xmax, xmax, -ymax, ymax, NEARCLIP, FARCLIP);
}

/*
=============
R_SetupGL
=============
*/
static void R_SetupGL (void)
{
	int	x, x2, y2, y, w, h;

	//
	// set up viewpoint
	//
	glMatrixMode_fp(GL_PROJECTION);
	glLoadIdentity_fp ();

	x  =  r_refdef.vrect.x * glwidth/vid.width;
	x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth/vid.width;
	y  = (vid.height - r_refdef.vrect.y) * glheight/vid.height;
	y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight/vid.height;

	// fudge around because of frac screen scale
	if (x > 0)
		x--;
	if (x2 < glwidth)
		x2++;
	if (y2 < 0)
		y2--;
	if (y < glheight)
		y++;

	w = x2 - x;
	h = y - y2;

	glViewport_fp (glx + x, gly + y2, w, h);
	GL_SetFrustum (r_refdef.fov_x, r_refdef.fov_y);

	if (mirror)
	{
		if (mirror_plane->normal[2])
			glScalef_fp (1, -1, 1);
		else
			glScalef_fp (-1, 1, 1);
		glCullFace_fp(GL_BACK);
	}
	else
		glCullFace_fp(GL_FRONT);

	glMatrixMode_fp(GL_MODELVIEW);
	glLoadIdentity_fp ();

	glRotatef_fp (-90,  1, 0, 0);	// put Z going up
	glRotatef_fp (90,  0, 0, 1);	// put Z going up
	glRotatef_fp (-r_refdef.viewangles[2],  1, 0, 0);
	glRotatef_fp (-r_refdef.viewangles[0],  0, 1, 0);
	glRotatef_fp (-r_refdef.viewangles[1],  0, 0, 1);
	glTranslatef_fp (-r_refdef.vieworg[0],  -r_refdef.vieworg[1],  -r_refdef.vieworg[2]);

	glGetFloatv_fp (GL_MODELVIEW_MATRIX, r_world_matrix);

	//
	// set drawing parms
	//
	if (gl_cull.integer)
		glEnable_fp(GL_CULL_FACE);
	else
		glDisable_fp(GL_CULL_FACE);

	glDisable_fp(GL_BLEND);
	glDisable_fp(GL_ALPHA_TEST);
	glEnable_fp(GL_DEPTH_TEST);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
static void R_RenderScene (void)
{
	R_SetupFrame ();

	R_SetFrustum ();

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld ();		// adds static entities to the list

	S_ExtraUpdate ();	// don't let sound get messed up if going slow

	R_DrawEntitiesOnList ();

	R_DrawAllGlows();

	R_RenderDlights ();
}


/*
=============
R_Clear
=============
*/
static void R_Clear (void)
{
	if (r_mirroralpha.value != 1.0)
	{
		if (gl_clear.integer)
			glClear_fp (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear_fp (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 0.5;
		glDepthFunc_fp (GL_LEQUAL);
	}
	else if (gl_ztrick.integer)
	{
		static int trickframe;

		if (gl_clear.integer)
			glClear_fp (GL_COLOR_BUFFER_BIT);

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			glDepthFunc_fp (GL_LEQUAL);
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			glDepthFunc_fp (GL_GEQUAL);
		}
	}
	else
	{
		if (gl_clear.integer)
			glClear_fp (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear_fp (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
		glDepthFunc_fp (GL_LEQUAL);
	}

	glDepthRange_fp (gldepthmin, gldepthmax);

	if (have_stencil && r_shadows.integer)
	{
		glClearStencil_fp(1);
		glClear_fp(GL_STENCIL_BUFFER_BIT);
	}
}


/*
=============
R_Mirror
=============
*/
static float	r_base_world_matrix[16];

/*
static void R_Mirror (void)
{
	float		d;
	msurface_t	*s;
	entity_t	*ent;

	if (!mirror)
		return;

	memcpy (r_base_world_matrix, r_world_matrix, sizeof(r_base_world_matrix));

	d = DotProduct (r_refdef.vieworg, mirror_plane->normal) - mirror_plane->dist;
	VectorMA (r_refdef.vieworg, -2*d, mirror_plane->normal, r_refdef.vieworg);

	d = DotProduct (vpn, mirror_plane->normal);
	VectorMA (vpn, -2*d, mirror_plane->normal, vpn);

	r_refdef.viewangles[0] = -asin (vpn[2])/M_PI*180;
	r_refdef.viewangles[1] = atan2 (vpn[1], vpn[0])/M_PI*180;
	r_refdef.viewangles[2] = -r_refdef.viewangles[2];

	ent = &cl_entities[cl.viewentity];
	if (cl_numvisedicts < MAX_VISEDICTS)
	{
		cl_visedicts[cl_numvisedicts] = ent;
		cl_numvisedicts++;
	}

	gldepthmin = 0.5;
	gldepthmax = 1;
	glDepthRange_fp (gldepthmin, gldepthmax);
	glDepthFunc_fp (GL_LEQUAL);

	R_RenderScene ();

	glDepthMask_fp(0);

	R_DrawParticles ();

// THIS IS THE F*S*D(KCING MIRROR ROUTINE!  Go down!!!
	R_DrawTransEntitiesOnList (true); // This restores the depth mask

	R_DrawWaterSurfaces ();

	R_DrawTransEntitiesOnList (false);

	gldepthmin = 0;
	gldepthmax = 0.5;
	glDepthRange_fp (gldepthmin, gldepthmax);
	glDepthFunc_fp (GL_LEQUAL);

	// blend on top
	glEnable_fp (GL_BLEND);
	glMatrixMode_fp(GL_PROJECTION);
	if (mirror_plane->normal[2])
		glScalef_fp (1,-1,1);
	else
		glScalef_fp (-1,1,1);
	glCullFace_fp(GL_FRONT);
	glMatrixMode_fp(GL_MODELVIEW);

	glLoadMatrixf_fp (r_base_world_matrix);

	glColor4f_fp (1,1,1,r_mirroralpha.value);
	s = cl.worldmodel->textures[mirrortexturenum]->texturechain;
	for ( ; s ; s = s->texturechain)
		R_RenderBrushPoly (&r_worldentity, s, true);
	cl.worldmodel->textures[mirrortexturenum]->texturechain = NULL;
	glDisable_fp (GL_BLEND);
	glColor4f_fp (1,1,1,1);
}
*/

/*
=============
R_PrintTimes
=============
*/
static void R_PrintTimes (void)
{
	float	r_time2;
	float	ms, fps;

	r_lasttime1 = r_time2 = Sys_DoubleTime();

	ms = 1000 * (r_time2 - r_time1);
	fps = 1000 / ms;

	Con_Printf("%3.1f fps %5.0f ms\n%4i wpoly  %4i epoly\n",
			fps, ms, c_brush_polys, c_alias_polys);
}


/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView (void)
{
	if (r_norefresh.integer)
		return;

	if (!r_worldentity.model || !cl.worldmodel)
		Sys_Error ("%s: NULL worldmodel", __thisfunc__);

	if (r_speeds.integer)
	{
		glFinish_fp ();
		if (r_wholeframe.integer)
			r_time1 = r_lasttime1;
		else
			r_time1 = Sys_DoubleTime ();
		c_brush_polys = 0;
		c_alias_polys = 0;
	}

	mirror = false;

//	glFinish_fp ();

	R_Clear ();

	// render normal view
	R_RenderScene ();

	glDepthMask_fp(0);

	R_DrawParticles ();

	R_DrawTransEntitiesOnList (r_viewleaf->contents == CONTENTS_EMPTY); // This restores the depth mask

	//R_DrawWaterSurfaces ();

	R_DrawTransEntitiesOnList (r_viewleaf->contents != CONTENTS_EMPTY);

	R_DrawViewModel();

	// render mirror view
	//R_Mirror ();

	R_PolyBlend ();

	if (r_speeds.integer)
		R_PrintTimes ();
}

