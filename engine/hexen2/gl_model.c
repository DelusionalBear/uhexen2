/*
 * model.c -- model loading and caching
 * models are the only shared resource between a client and server
 * running on the same machine.
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
#include "hwal.h"

qmodel_t	*loadmodel;
static char	loadname[MAX_QPATH];	// for hunk tags

static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash);
static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer);
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer);
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer);
static void Mod_LoadAliasModelNew (qmodel_t *mod, void *buffer);

static void Mod_Print (void);

static cvar_t	external_ents = {"external_ents", "1", CVAR_ARCHIVE};

//static byte	mod_novis[MAX_MAP_LEAFS/8];
static byte	*mod_novis;
static int	mod_novis_capacity;

static byte	*mod_decompressed;
static int	mod_decompressed_capacity;

// 650 should be enough with model handle recycling, but.. (Pa3PyX)
#define	MAX_MOD_KNOWN	4096	//spike: boosted for crazy bsp2 maps
static qmodel_t	mod_known[MAX_MOD_KNOWN];
static int	mod_numknown;

static vec3_t	aliasmins, aliasmaxs;

int		entity_file_size;

static qboolean	spr_reload_only = false;

texture_t	*r_notexture_mip; //johnfitz -- moved here from r_main.c
texture_t	*r_notexture_mip2; //johnfitz -- used for non-lightmapped surfs with a missing texture


/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&external_ents);
	Cmd_AddCommand ("mcache", Mod_Print);

	//memset (mod_novis, 0xff, sizeof(mod_novis));

	//johnfitz -- create notexture miptex
	r_notexture_mip = (texture_t *)Hunk_AllocName(sizeof(texture_t), "r_notexture_mip");
	strcpy(r_notexture_mip->name, "notexture");
	r_notexture_mip->height = r_notexture_mip->width = 32;

	r_notexture_mip2 = (texture_t *)Hunk_AllocName(sizeof(texture_t), "r_notexture_mip2");
	strcpy(r_notexture_mip2->name, "notexture2");
	r_notexture_mip2->height = r_notexture_mip2->width = 32;
	//johnfitz
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (qmodel_t *mod)
{
	void	*r;

	r = Cache_Check (&mod->cache);
	if (r)
		return r;

	Mod_LoadModel (mod, true);

	if (!mod->cache.data)
		Sys_Error ("%s: caching failed", __thisfunc__);
	return mod->cache.data;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, qmodel_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;

	if (!model)
		Sys_Error ("%s: NULL model", __thisfunc__);
	if (!model->nodes)
		Sys_Error ("%s: model w/o nodes : %s", __thisfunc__, model->name);

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
static byte *Mod_DecompressVis (byte *in, qmodel_t *model)
{
	int		c;
	byte	*out;
	byte	*outend;
	int		row;

	row = (model->numleafs + 7) >> 3;
	if (mod_decompressed == NULL || row > mod_decompressed_capacity)
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *)realloc(mod_decompressed, mod_decompressed_capacity);
		if (!mod_decompressed)
			Sys_Error("Mod_DecompressVis: realloc() failed on %d bytes", mod_decompressed_capacity);
	}
	out = mod_decompressed;
	outend = mod_decompressed + row;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return mod_decompressed;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;
		while (c)
		{
			if (out == outend)
			{
				//if (!model->viswarn) {
				//	model->viswarn = true;
				//	Con_Warning("Mod_DecompressVis: output overrun on model \"%s\"\n", model->name);
				//}
				return mod_decompressed;
			}
			*out++ = 0;
			c--;
		}
	} while (out - mod_decompressed < row);

	return mod_decompressed;
}

byte *Mod_NoVisPVS(qmodel_t *model);

byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model)
{
	if (leaf == model->leafs)
		return Mod_NoVisPVS(model);
		//return mod_novis;
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

byte *Mod_NoVisPVS(qmodel_t *model)
{
	int pvsbytes;

	pvsbytes = (model->numleafs + 7) >> 3;
	if (mod_novis == NULL || pvsbytes > mod_novis_capacity)
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *)realloc(mod_novis, mod_novis_capacity);
		if (!mod_novis)
			Sys_Error("Mod_NoVisPVS: realloc() failed on %d bytes", mod_novis_capacity);

		memset(mod_novis, 0xff, mod_novis_capacity);
	}
	return mod_novis;
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	qmodel_t	*mod;

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{	// clear alias models only if textures were flushed (Pa3PyX)
		if (mod->type == mod_alias)
		{
			if (flush_textures && gl_purge_maptex.integer)
			{
				if (Cache_Check(&(mod->cache)))
					Cache_Free(&(mod->cache), true); //johnfitz -- added second argument
				mod->needload = NL_NEEDS_LOADED;
			}
		}
		else
		{	// Clear all other models completely
			TexMgr_FreeTexturesForOwner(mod); //johnfitz
			memset(mod, 0, sizeof(qmodel_t));
			mod->needload = NL_UNREFERENCED;
		}
	}
}


void Mod_ResetAll(void)
{
	int		i;
	qmodel_t	*mod;

	//ericw -- free alias model VBOs
	//GLMesh_DeleteVertexBuffers();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->needload) //otherwise Mod_ClearAll() did it already
			TexMgr_FreeTexturesForOwner(mod);
		memset(mod, 0, sizeof(qmodel_t));
	}
	mod_numknown = 0;
}

/*
==================
Mod_FindName

==================
*/
qmodel_t *Mod_FindName (const char *name)
{
	int		i;
	qmodel_t	*mod = NULL;

	if (!name[0])
		Sys_Error ("%s: NULL name", __thisfunc__);

//
// search the currently loaded models
//
	// allow recycling of model handles (Pa3PyX)
	for (i = 0; i < mod_numknown; i++)
	{
		if (!strcmp(mod_known[i].name, name))
		{
			mod = &(mod_known[i]);
			return mod;
		}
		if (!mod && mod_known[i].needload == NL_UNREFERENCED)
		{
			mod = mod_known + i;
		}
	}

	if (!mod)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{	// No free model handle
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		}
		else
		{
			mod = &(mod_known[mod_numknown++]);
		}
	}
	q_strlcpy (mod->name, name, MAX_QPATH);
	mod->needload = NL_NEEDS_LOADED;
	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (const char *name)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	if (mod->needload == NL_PRESENT)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash)
{
	byte	*buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap
	int	mod_type;

	// allow recycling of models (Pa3PyX)
	if (mod->type == mod_alias)
	{
		if (Cache_Check(&mod->cache))
		{
			mod->needload = NL_PRESENT;
			return mod;
		}
	}
	else if (mod->needload == NL_PRESENT)
	{
		return mod;
	}

//
// load the file
//
	buf = FS_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf), & mod->path_id);
	if (!buf)
	{
		if (crash)
			Sys_Error ("%s: %s not found", __thisfunc__, mod->name);
		return NULL;
	}

//
// allocate a new model
//
	COM_FileBase (mod->name, loadname, sizeof(loadname));

	loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
	mod->needload = NL_PRESENT;

	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	switch (mod_type)
	{
	case RAPOLYHEADER:
		Mod_LoadAliasModelNew (mod, buf);
		break;
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf);
		break;
	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;
	default:
		Mod_LoadBrushModel (mod, buf);
		break;
	}

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
qmodel_t *Mod_ForName (const char *name, qboolean crash)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

static byte	*mod_base;


/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, maxanim, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t *m;
	//johnfitz -- more variables
	char		texturename[64];
	int			nummiptex;
	src_offset_t		offset;
	int			mark, fwidth, fheight;
	char		filename[MAX_OSPATH], filename2[MAX_OSPATH], mapname[MAX_OSPATH];
	byte		*data;
	extern byte *hunk_base;
	//johnfitz

#ifdef WAL_TEXTURES
	// external WAL texture loading
	char		texname[MAX_QPATH];
	int		mark;
	miptex_wal_t	*mt_wal;
#endif

	//johnfitz -- don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		Con_Printf("Mod_LoadTextures: no textures in bsp file\n");
		nummiptex = 0;
		m = NULL; // avoid bogus compiler warning
	}
	else
	{
		m = (dmiptexlump_t *)(mod_base + l->fileofs);
		m->nummiptex = LittleLong(m->nummiptex);
		nummiptex = m->nummiptex;
	}
	//johnfitz


	loadmodel->numtextures = nummiptex + 2; //johnfitz -- need 2 dummy texture chains for missing textures
	loadmodel->textures = (texture_t **)Hunk_AllocName(loadmodel->numtextures * sizeof(*loadmodel->textures), loadname);

	for (i = 0; i < m->nummiptex; i++)
	{
		m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j = 0; j < MIPLEVELS; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);

#ifdef WAL_TEXTURES
		if (!r_texture_external.integer)
			goto bsp_tex_internal;
		// try an external wal texture file first
		q_snprintf (texname, sizeof(texname), "textures/%s.wal", mt->name);
		if (texname[sizeof(WAL_EXT_DIRNAME)] == '*')
			texname[sizeof(WAL_EXT_DIRNAME)] = WAL_REPLACE_ASTERIX;
		mark = Hunk_LowMark ();
		mt_wal = (miptex_wal_t *)FS_LoadHunkFile(texname, NULL);
		if (mt_wal != NULL)
		{
			mt_wal->ident = LittleLong (mt_wal->ident);
			mt_wal->version = LittleLong (mt_wal->version);
			if (mt_wal->ident == IDWALHEADER && mt_wal->version == WALVERSION)
			{
				mt_wal->width = LittleLong (mt_wal->width);
				mt_wal->height = LittleLong (mt_wal->height);
				for (j = 0; j < MIPLEVELS; j++)
					mt_wal->offsets[j] = LittleLong (mt_wal->offsets[j]);
				if ( (mt_wal->width & 15) || (mt_wal->height & 15) )
				{
					Hunk_FreeToLowMark (mark);
					Sys_Printf ("Texture %s is not 16 aligned", texname);
					goto bsp_tex_internal;
				}

				pixels = mt_wal->width*mt_wal->height/64*85;
				tx = (texture_t *) Hunk_AllocName (sizeof(texture_t) +pixels, "texture");
				loadmodel->textures[i] = tx;

				memcpy (tx->name, mt_wal->name, sizeof(tx->name));

				tx->width = mt_wal->width;
				tx->height = mt_wal->height;
				for (j = 0; j < MIPLEVELS; j++)
					tx->offsets[j] = mt_wal->offsets[j] + sizeof(texture_t) - sizeof(miptex_wal_t);
				// the pixels immediately follow the structures
				memcpy (tx+1, mt_wal+1, pixels);
			}
			else
			{
				if (mt_wal->ident != IDWALHEADER)
					Sys_Printf ("%s: %s is not a valid WAL file\n", __thisfunc__, texname);
				if (mt_wal->version != WALVERSION)
					Sys_Printf ("%s: WAL file %s has unsupported version (%d)\n", __thisfunc__, texname, mt_wal->version);
				Hunk_FreeToLowMark (mark);
				goto bsp_tex_internal;
			}
		}
		else
		{	// load internal bsp pixel data
bsp_tex_internal:
#endif /* WAL_TEXTURES */

			
			if ( (mt->width & 15) || (mt->height & 15) )
				Sys_Error ("Texture %s is not 16 aligned", mt->name);
			pixels = mt->width*mt->height/64*85;
			tx = (texture_t *) Hunk_AllocName (sizeof(texture_t) +pixels, "texture" );
			loadmodel->textures[i] = tx;

			memcpy (tx->name, mt->name, sizeof(tx->name));
			tx->width = mt->width;
			tx->height = mt->height;
			for (j = 0; j < MIPLEVELS; j++)
				tx->offsets[j] = mt->offsets[j] + sizeof(texture_t) - sizeof(miptex_t);
			// the pixels immediately follow the structures

		// ericw -- check for pixels extending past the end of the lump.
		// appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
		// kellbase1.bsp (quoth), and can lead to a segfault if we read past
		// the end of the .bsp file buffer
			if (((byte*)(mt + 1) + pixels) > (mod_base + l->fileofs + l->filelen))
			{
				Con_DPrintf("Texture %s extends past end of lump\n", mt->name);
				pixels = q_max(0, (mod_base + l->fileofs + l->filelen) - (byte*)(mt + 1));
			}
			memcpy(tx + 1, mt + 1, pixels);

			//tx->update_warp = false; //johnfitz
			//tx->warpimage = NULL; //johnfitz
			//tx->fullbright = NULL; //johnfitz

#ifdef WAL_TEXTURES
		}
#endif

#if !defined (H2W)
		if (cls.state == ca_dedicated)
			continue;
#endif	/* H2W */

		//johnfitz -- lots of changes
		if (!isDedicated) //no texture uploading for dedicated server
		{
			if (!q_strncasecmp(tx->name, "sky", 3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
				Sky_LoadTexture(tx);
			else if (tx->name[0] == '*') //warping texture
			{
				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark();
				COM_StripExtension(loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf(filename, sizeof(filename), "textures/%s/#%s", mapname, tx->name + 1); //this also replaces the '*' with a '#'
				data = Image_LoadImage(filename, &fwidth, &fheight);
				if (!data)
				{
					q_snprintf(filename, sizeof(filename), "textures/#%s", tx->name + 1);
					data = Image_LoadImage(filename, &fwidth, &fheight);
				}

				//now load whatever we found
				if (data) //load external image
				{
					q_strlcpy(texturename, filename, sizeof(texturename));
					tx->gltexture = TexMgr_LoadImage(loadmodel, texturename, fwidth, fheight,
						SRC_RGBA, data, filename, 0, TEXPREF_NONE);
				}
				else //use the texture from the bsp file
				{
					q_snprintf(texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					offset = (src_offset_t)(mt + 1) - (src_offset_t)mod_base;
					tx->gltexture = TexMgr_LoadImage(loadmodel, texturename, tx->width, tx->height,
						SRC_INDEXED, (byte *)(tx + 1), loadmodel->name, offset, TEXPREF_NONE);
				}

				//now create the warpimage, using dummy data from the hunk to create the initial image
				Hunk_Alloc(gl_warpimagesize*gl_warpimagesize * 4); //make sure hunk is big enough so we don't reach an illegal address
				Hunk_FreeToLowMark(mark);
				q_snprintf(texturename, sizeof(texturename), "%s_warp", texturename);
				tx->warpimage = TexMgr_LoadImage(loadmodel, texturename, gl_warpimagesize,
					gl_warpimagesize, SRC_RGBA, hunk_base, "", (src_offset_t)hunk_base, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
				tx->update_warp = true;
			}
			else //regular texture
			{
				// ericw -- fence textures
				int	extraflags;

				extraflags = 0;
				if (tx->name[0] == '{')
					extraflags |= TEXPREF_ALPHA;
				// ericw

				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark();
				COM_StripExtension(loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf(filename, sizeof(filename), "textures/%s/%s", mapname, tx->name);
				data = Image_LoadImage(filename, &fwidth, &fheight);
				if (!data)
				{
					q_snprintf(filename, sizeof(filename), "textures/%s", tx->name);
					data = Image_LoadImage(filename, &fwidth, &fheight);
				}

				//now load whatever we found
				if (data) //load external image
				{
					tx->gltexture = TexMgr_LoadImage(loadmodel, filename, fwidth, fheight,
						SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags);

					//now try to load glow/luma image from the same place
					Hunk_FreeToLowMark(mark);
					q_snprintf(filename2, sizeof(filename2), "%s_glow", filename);
					data = Image_LoadImage(filename2, &fwidth, &fheight);
					if (!data)
					{
						q_snprintf(filename2, sizeof(filename2), "%s_luma", filename);
						data = Image_LoadImage(filename2, &fwidth, &fheight);
					}

					if (data)
						tx->fullbright = TexMgr_LoadImage(loadmodel, filename2, fwidth, fheight,
							SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags);
				}
				else //use the texture from the bsp file
				{
					q_snprintf(texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					offset = (src_offset_t)(mt + 1) - (src_offset_t)mod_base;
					//if (Mod_CheckFullbrights((byte *)(tx + 1), pixels))
					//{
					//	tx->gltexture = TexMgr_LoadImage(loadmodel, texturename, tx->width, tx->height,
					//		SRC_INDEXED, (byte *)(tx + 1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
					//	q_snprintf(texturename, sizeof(texturename), "%s:%s_glow", loadmodel->name, tx->name);
					//	tx->fullbright = TexMgr_LoadImage(loadmodel, texturename, tx->width, tx->height,
					//		SRC_INDEXED, (byte *)(tx + 1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
					//}
					//else
					{
						tx->gltexture = TexMgr_LoadImage(loadmodel, texturename, tx->width, tx->height,
							SRC_INDEXED, (byte *)(tx + 1), loadmodel->name, offset, TEXPREF_MIPMAP | extraflags);
					}
				}
				Hunk_FreeToLowMark(mark);
			}
		}
		//johnfitz

		/*
		// ericw -- fence textures
		int	extraflags;

		extraflags = 0;
		if (tx->name[0] == '{')
			extraflags |= TEX_HOLEY;
		// ericw

		if (!q_strncasecmp(mt->name, "sky", 3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
			Sky_LoadTexture(tx);
		else
			tx->gltexture = TexMgr_LoadImage(loadmodel, tx->name, tx->width, tx->height, SRC_INDEXED, (byte *)(tx + 1),
				WADFILENAME, 0, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP);
			//tx->gl_texturenum = GL_LoadTexture (mt->name, (byte *)(tx+1), tx->width, tx->height, (TEX_MIPMAP | extraflags));
		*/
	}

	//johnfitz -- last 2 slots in array should be filled with dummy textures
	loadmodel->textures[loadmodel->numtextures - 2] = r_notexture_mip; //for lightmapped surfs
	loadmodel->textures[loadmodel->numtextures - 1] = r_notexture_mip2; //for SURF_DRAWTILED surfs

//
// sequence the animations
//
	for (i = 0; i < m->nummiptex; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// already sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		maxanim = tx->name[1];
		altmax = 0;
		if (maxanim >= 'a' && maxanim <= 'z')
			maxanim -= 'a' - 'A';
		if (maxanim >= '0' && maxanim <= '9')
		{
			maxanim -= '0';
			altmax = 0;
			anims[maxanim] = tx;
			maxanim++;
		}
		else if (maxanim >= 'A' && maxanim <= 'J')
		{
			altmax = maxanim - 'A';
			maxanim = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j = i+1; j < m->nummiptex; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > maxanim)
					maxanim = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

#define	ANIM_CYCLE	2
	// link them all together
		for (j = 0; j < maxanim; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = maxanim * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%maxanim ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j = 0; j < altmax; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (maxanim)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_ReloadTextures

Pa3PyX: Analogous to Mod_LoadTextures() below, except that we already
have all models and textures allocated, only need to rebind and re-
upload them to OpenGL pipeline. Called when video mode is changed upon
which all OpenGL textures are gone.
=================
*/
void Mod_ReloadTextures (void)
{
	int			j;
	qmodel_t	*mod;
	texture_t	*tx;

	// Reload world (brush models are submodels of world),
	// don't touch if not yet loaded
	mod = cl.worldmodel;
	if (mod && !mod->needload)
	{
		for (j = 0; j < mod->numtextures; j++)
		{
			tx = mod->textures[j];
			if (tx)
			{
				if (!q_strncasecmp(tx->name, "sky", 3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
					Sky_LoadTexture(tx);
				else
					tx->gltexture = TexMgr_LoadImage(loadmodel, tx->name, tx->width, tx->height, SRC_INDEXED, (byte *)(tx + 1),
						WADFILENAME, 0, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP);
			}
		}
	}

	// Reload alias models and sprites
	for (j = 0; j < mod_numknown; j++)
	{
		if ((mod_known[j].type == mod_alias) && (mod_known[j].needload != NL_UNREFERENCED))
		{
			if (Cache_Check(&(mod_known[j].cache)))
				Cache_Free(&(mod_known[j].cache), true); //johnfitz -- added second argument
			mod_known[j].needload = NL_NEEDS_LOADED;
			Mod_LoadModel(mod_known + j, false);
		}
		else if ((mod_known[j].type == mod_sprite) && (mod_known[j].needload != NL_UNREFERENCED))
		{
			mod_known[j].needload = NL_NEEDS_LOADED;
			spr_reload_only = true;
			Mod_LoadModel(mod_known + j, false);
			spr_reload_only = false;
		}
	}

	// Reload player skins
	if (cls.state == ca_active)
	{
		for (j = 0; j < cl.maxclients && j < cl.num_entities + 1; j++)
		{
			R_TranslatePlayerSkin(j);
		}
	}
}

/*
=================
Mod_LoadLighting
=================
*/
static void Mod_LoadLighting (lump_t *l)
{
	GL_SetupLightmapFmt();	// setup the lightmap format to reflect any
				// changes via the cvar gl_lightmapfmt

	// bound the gl_coloredlight value
	if (gl_coloredlight.integer < 0)
		Cvar_Set ("gl_coloredlight", "0");
	gl_coloredstatic = gl_coloredlight.integer;

	if (gl_lightmap_format == GL_RGBA)
	{
		int	i;
		byte	*in, *out, *data;
		byte	d;

		loadmodel->lightdata = NULL;

		if (gl_coloredlight.integer)
		{	// LordHavoc: check for a .lit file
			int		mark;
			char	litfilename[MAX_QPATH];
			unsigned int	path_id;

			q_strlcpy(litfilename, loadmodel->name, sizeof(litfilename));
			COM_StripExtension(litfilename, litfilename, sizeof(litfilename));
			q_strlcat(litfilename, ".lit", sizeof(litfilename));
			Con_DPrintf("trying to load %s\n", litfilename);
			mark = Hunk_LowMark();
			data = (byte*) FS_LoadHunkFile (litfilename, &path_id);
			if (data == NULL)
				goto _load_internal;
			// use lit file only from the same gamedir as the map
			// itself or from a searchpath with higher priority.
			if (path_id < loadmodel->path_id)
			{
				Hunk_FreeToLowMark(mark);
				Con_DPrintf("ignored %s from a gamedir with lower priority\n", litfilename);
				goto _load_internal;
			}
			if (data[0] != 'Q' || data[1] != 'L' || data[2] != 'I' || data[3] != 'T')
			{
				Hunk_FreeToLowMark(mark);
				Con_Printf("Corrupt .lit file (old version?), ignoring\n");
				goto _load_internal;
			}
			i = LittleLong(((int *)data)[1]);
			if (i != 1)
			{
				Hunk_FreeToLowMark(mark);
				Con_Printf("Unknown .lit file version (%d)\n", i);
				goto _load_internal;
			}
			Con_DPrintf("%s loaded\n", litfilename);
			Con_DPrintf("Loaded colored light (32-bit)\n");
			if (gl_coloredlight.integer == 1)
			{
				loadmodel->lightdata = data + 8;
				return;
			}
			else if (!l->filelen)
			{
				loadmodel->lightdata = data + 8;
				Con_Printf("No white light data. Using colored only\n");
				return;
			}
			else	// experimental blend code for gl_coloredlight.integer == 2
			{
				int	min_light = 8;
				int	k = 0;
				int	j, r, g, b;
				float	l2lc = 0;
				float	lc = 0;
				float	li = 0;

				// allocate memory and load light data from .bsp
				mark = Hunk_LowMark();
				loadmodel->lightdata = (byte *) Hunk_AllocName (l->filelen, "light");
				memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);

				for (i = 0, j = 0, k = 0; i < l->filelen * 3; i += 3, j += 3)
				{
					// set some minimal light level
					r = q_max(data[8+i  ], min_light);
					g = q_max(data[8+i+1], min_light);
					b = q_max(data[8+i+2], min_light);

					// compute brightness of colored ligths present in .lit file
					lc = (r + g + b) / 3.0f;
					li = (float) loadmodel->lightdata[k];
					if (li == 0)
						li = min_light;
					if (lc == 0)
						lc = min_light;

					// compute light amplification level
					l2lc = li / lc;
					if (l2lc < 1.5f)
						l2lc = 1.0f;

					// update colors
					data[8+j]   = (byte) q_min (q_max(ceil(r*l2lc), min_light), 255);
					data[8+j+1] = (byte) q_min (q_max(ceil(g*l2lc), min_light), 255);
					data[8+j+2] = (byte) q_min (q_max(ceil(b*l2lc), min_light), 255);
					k++;
				}
				Hunk_FreeToLowMark(mark);

				loadmodel->lightdata = data + 8;
				Con_DPrintf("Blended colored and white light.\n");
				return;
			}
		}
  _load_internal:
		// no .lit found, expand the white lighting data to color
		if (!l->filelen)
			return;
		loadmodel->lightdata = (byte *) Hunk_AllocName (l->filelen*3, "light");
		in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
		out = loadmodel->lightdata;
		memcpy (in, mod_base + l->fileofs, l->filelen);
		for (i = 0; i < l->filelen; i++)
		{
			d = *in++;
			*out++ = d;
			*out++ = d;
			*out++ = d;
		}
		Con_DPrintf("Loaded white light (32-bit)\n");
	}
	else
	{
		if (!l->filelen)
		{
			loadmodel->lightdata = NULL;
			return;
		}
		loadmodel->lightdata = (byte *) Hunk_AllocName ( l->filelen, "light");
		memcpy (loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
		Con_DPrintf("Loaded white light (8-bit)\n");
	}
}


/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility (lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}
	loadmodel->visdata = (byte *) Hunk_AllocName ( l->filelen, "vis");
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities (lump_t *l)
{
	char	entfilename[MAX_QPATH];
	char		*ents;
	int		mark;
	unsigned int	path_id;

	if (! external_ents.integer)
		goto _load_embedded;

	q_strlcpy(entfilename, loadmodel->name, sizeof(entfilename));
	COM_StripExtension(entfilename, entfilename, sizeof(entfilename));
	q_strlcat(entfilename, ".ent", sizeof(entfilename));
	Con_DPrintf("trying to load %s\n", entfilename);
	mark = Hunk_LowMark();
	ents = (char *) FS_LoadHunkFile (entfilename, &path_id);
	if (ents)
	{
		// use ent file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", entfilename);
		}
		else
		{
			entity_file_size = fs_filesize;
			loadmodel->entities = ents;
			Con_DPrintf("Loaded external entity file %s\n", entfilename);
			return;
		}
	}

_load_embedded:
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	loadmodel->entities = (char *) Hunk_AllocName ( l->filelen, "entities");
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
	entity_file_size = l->filelen;
}


/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (dvertex_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mvertex_t *) Hunk_AllocName (count * sizeof(*out), "vertexes");

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*out;
	int			i, j, count;

	dq1model_t	*inq1 = (dq1model_t *)(mod_base + l->fileofs);
	if (inq1->numfaces)	//aka: inh2->headnode[6] - hexen2 only uses 6 of its 8 hull slots, and we can detect that.
	{
		dq1model_t	*in = inq1;
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (dmodel_t *) Hunk_AllocName (count * sizeof(*out), "submodels");

		loadmodel->submodels = out;
		loadmodel->numsubmodels = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			for (j = 0; j < 3; j++)
			{	// spread the mins / maxs by a pixel
				out->mins[j] = LittleFloat (in->mins[j]) - 1;
				out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
				out->origin[j] = LittleFloat (in->origin[j]);
			}
			for (j = 0; j < 4; j++)
				out->headnode[j] = LittleLong (in->headnode[j]);
			for (; j < MAX_MAP_HULLS; j++)
				out->headnode[j] = out->headnode[0];
			out->visleafs = LittleLong (in->visleafs);
			out->firstface = LittleLong (in->firstface);
			out->numfaces = LittleLong (in->numfaces);
		}
	}
	else
	{
		dmodel_t	*in = (dmodel_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (dmodel_t *) Hunk_AllocName (count * sizeof(*out), "submodels");

		loadmodel->submodels = out;
		loadmodel->numsubmodels = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			for (j = 0; j < 3; j++)
			{	// spread the mins / maxs by a pixel
				out->mins[j] = LittleFloat (in->mins[j]) - 1;
				out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
				out->origin[j] = LittleFloat (in->origin[j]);
			}
			for (j = 0; j < MAX_MAP_HULLS; j++)
				out->headnode[j] = LittleLong (in->headnode[j]);
			out->visleafs = LittleLong (in->visleafs);
			out->firstface = LittleLong (in->firstface);
			out->numfaces = LittleLong (in->numfaces);
		}
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges (lump_t *l, qboolean lm)
{
	medge_t *out;
	int	i, count;

	if (lm)
	{
		dledge_t *in = (dledge_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocName ((count + 1) * sizeof(*out), "edges");

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			out->v[0] = (unsigned int)LittleLong(in->v[0]);
			out->v[1] = (unsigned int)LittleLong(in->v[1]);
		}
	}
	else
	{
		dsedge_t *in = (dsedge_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocName ((count + 1) * sizeof(*out), "edges");

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			out->v[0] = (unsigned short)LittleShort(in->v[0]);
			out->v[1] = (unsigned short)LittleShort(in->v[1]);
		}
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
static void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int	i, j, count;
	int		miptex;
	float	len1, len2;

	in = (texinfo_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mtexinfo_t *) Hunk_AllocName (count * sizeof(*out), "texture");

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (j = 0; j < 4; j++)
		{
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat (in->vecs[1][j]);
		}
		len1 = VectorLength (out->vecs[0]);
		len2 = VectorLength (out->vecs[1]);
		len1 = (len1 + len2)/2;
		if (len1 < 0.32)
			out->mipadjust = 4;
		else if (len1 < 0.49)
			out->mipadjust = 3;
		else if (len1 < 0.99)
			out->mipadjust = 2;
		else
			out->mipadjust = 1;
#if 0
		if (len1 + len2 < 0.001)
			out->mipadjust = 1;		// don't crash
		else
			out->mipadjust = 1 / floor( (len1+len2)/2 + 0.1 );
#endif

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);

		if (!loadmodel->textures)
		{
			out->texture = r_notexture_mip;	// checkerboard texture
			out->flags = 0;
		}
		else
		{
			if (miptex >= loadmodel->numtextures)
				Sys_Error ("miptex >= loadmodel->numtextures");
			out->texture = loadmodel->textures[miptex];
			if (!out->texture)
			{
				out->texture = r_notexture_mip; // texture not found
				out->flags = 0;
			}
		}
	}
}

/*
================
Mod_PolyForUnlitSurface -- johnfitz -- creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into BuildSurfaceDisplayList?
================
*/
void Mod_PolyForUnlitSurface(msurface_t *fa)
{
	vec3_t		verts[64];
	int			numverts, i, lindex;
	float		*vec;
	glpoly_t	*poly;
	float		texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0 / 128.0); //warp animation repeats every 128
	else
		texscale = (1.0 / 32.0); //to match r_notexture_mip

	// convert edges back to a normal polygon
	numverts = 0;
	for (i = 0; i < fa->numedges; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];

		if (lindex > 0)
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		else
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		VectorCopy(vec, verts[numverts]);
		numverts++;
	}

	//create the poly
	poly = (glpoly_t *)Hunk_Alloc(sizeof(glpoly_t) + (numverts - 4) * VERTEXSIZE * sizeof(float));
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;
	for (i = 0, vec = (float *)verts; i < numverts; i++, vec += 3)
	{
		VectorCopy(vec, poly->verts[i]);
		poly->verts[i][3] = DotProduct(vec, fa->texinfo->vecs[0]) * texscale;
		poly->verts[i][4] = DotProduct(vec, fa->texinfo->vecs[1]) * texscale;
	}
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
void CalcSurfaceExtents(msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i, j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i = 0; i < s->numedges; i++)
	{
		e = loadmodel->surfedges[s->firstedge + i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for (j = 0; j < 2; j++)
		{
			/* The following calculation is sensitive to floating-point
			 * precision.  It needs to produce the same result that the
			 * light compiler does, because R_BuildLightMap uses surf->
			 * extents to know the width/height of a surface's lightmap,
			 * and incorrect rounding here manifests itself as patches
			 * of "corrupted" looking lightmaps.
			 * Most light compilers are win32 executables, so they use
			 * x87 floating point.  This means the multiplies and adds
			 * are done at 80-bit precision, and the result is rounded
			 * down to 32-bits and stored in val.
			 * Adding the casts to double seems to be good enough to fix
			 * lighting glitches when Quakespasm is compiled as x86_64
			 * and using SSE2 floating-point.  A potential trouble spot
			 * is the hallway at the beginning of mfxsp17.  -- ericw
			 */
			val = ((double)v->position[0] * (double)tex->vecs[j][0]) +
				((double)v->position[1] * (double)tex->vecs[j][1]) +
				((double)v->position[2] * (double)tex->vecs[j][2]) +
				(double)tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor(mins[i] / 16);
		bmaxs[i] = ceil(maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if (!(tex->flags & TEX_SPECIAL) && s->extents[i] > 2000) //johnfitz -- was 512 in glquake, 256 in winquake
			Sys_Error("Bad surface extents");
	}
}

/*
=================
Mod_LoadFaces
=================
*/
static void Mod_LoadFaces (lump_t *l, qboolean lm)
{
	dsface_t		*ins = NULL;
	dlface_t		*inl = NULL;
	msurface_t	*out;
	int			i, count, surfnum;
	int			planenum, side, lightofs, texinfo;

	if (lm)
	{
		inl = (dlface_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*inl))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*inl);
	}
	else
	{
		ins = (dsface_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*ins))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*ins);
	}
	out = (msurface_t *) Hunk_AllocName (count * sizeof(*out), "faces");

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for (surfnum = 0; surfnum < count; surfnum++, out++)
	{
		if (lm)
		{
			out->firstedge = LittleLong(inl->firstedge);
			out->numedges = LittleLong(inl->numedges);
			planenum = LittleLong(inl->planenum);
			side = LittleLong(inl->side);
			texinfo = LittleLong(inl->texinfo);
			lightofs = LittleLong(inl->lightofs);
			for (i = 0; i < MAXLIGHTMAPS; i++)
				out->styles[i] = inl->styles[i];
			inl++;
		}
		else
		{
			out->firstedge = LittleLong(ins->firstedge);
			out->numedges = LittleShort(ins->numedges);
			planenum = LittleShort(ins->planenum);
			side = LittleShort(ins->side);
			texinfo = LittleShort (ins->texinfo);
			lightofs = LittleLong(ins->lightofs);
			for (i = 0; i < MAXLIGHTMAPS; i++)
				out->styles[i] = ins->styles[i];
			ins++;
		}
		out->flags = 0;

		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + texinfo;

		CalcSurfaceExtents (out);

	// lighting info
		if (lightofs  == -1)
		{
			out->samples = NULL;
		}
		else
		{
			//out->samples = loadmodel->lightdata + lightofs ;
			if (gl_lightmap_format == GL_RGBA)
				out->samples = loadmodel->lightdata + (lightofs  * 3);
			else
				out->samples = loadmodel->lightdata + lightofs ;
		}

		//johnfitz -- this section rewritten
		if (!q_strncasecmp(out->texinfo->texture->name, "sky", 3)) // sky surface //also note -- was Q_strncmp, changed to match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			Mod_PolyForUnlitSurface(out); //no more subdivision
		}
		else if (out->texinfo->texture->name[0] == '*')		// turbulent
		{
			out->flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
			for (i = 0; i < 2; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}

#ifndef QUAKE2
			GL_SubdivideSurface (out);	// cut up polygon for warps
#endif
			//Mod_PolyForUnlitSurface(out);
			GL_SubdivideSurface(out);

			if ( (!q_strncasecmp(out->texinfo->texture->name,"*rtex078",8)) ||
					(!q_strncasecmp(out->texinfo->texture->name,"*lowlight",9)) )
				out->flags |= SURF_TRANSLUCENT;

		}
		else if (out->texinfo->texture->name[0] == '{') // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
		}
	}
}


/*
=================
Mod_SetParent
=================
*/
static void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes (lump_t *l, qboolean lm)
{
	int			i, j, count, p;
	mnode_t		*out;

	if (lm)
	{
		dlnode_t		*in = (dlnode_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (mnode_t *) Hunk_AllocName (count * sizeof(*out), "nodes");

		loadmodel->nodes = out;
		loadmodel->numnodes = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			for (j = 0; j < 3; j++)
			{
				out->minmaxs[j] = LittleFloat (in->mins[j]);
				out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
			}

			p = LittleLong(in->planenum);
			out->plane = loadmodel->planes + p;

			out->firstsurface = LittleLong (in->firstface);
			out->numsurfaces = LittleLong (in->numfaces);

			for (j = 0; j < 2; j++)
			{
				p = LittleLong (in->children[j]);
				if (p >= 0)
					out->children[j] = loadmodel->nodes + p;
				else
					out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
			}
		}
	}
	else
	{
		dsnode_t		*in = (dsnode_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (mnode_t *) Hunk_AllocName (count * sizeof(*out), "nodes");

		loadmodel->nodes = out;
		loadmodel->numnodes = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			for (j = 0; j < 3; j++)
			{
				out->minmaxs[j] = LittleShort (in->mins[j]);
				out->minmaxs[3+j] = LittleShort (in->maxs[j]);
			}

			p = LittleLong(in->planenum);
			out->plane = loadmodel->planes + p;

			out->firstsurface = LittleShort (in->firstface);
			out->numsurfaces = LittleShort (in->numfaces);

			for (j = 0; j < 2; j++)
			{
				p = LittleShort (in->children[j]);
				if (p >= 0)
					out->children[j] = loadmodel->nodes + p;
				else
					out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
			}
		}
	}

	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs (lump_t *l, qboolean lm)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (lm)
	{
		dlleaf_t		*in = (dlleaf_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), "leafs");

		loadmodel->leafs = out;
		loadmodel->numleafs = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			for (j = 0; j < 3; j++)
			{
				out->minmaxs[j] = LittleFloat (in->mins[j]);
				out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
			}

			p = LittleLong(in->contents);
			out->contents = p;

			out->firstmarksurface = loadmodel->marksurfaces + LittleLong(in->firstmarksurface);
			out->nummarksurfaces = LittleLong(in->nummarksurfaces);

			p = LittleLong(in->visofs);
			if (p == -1)
				out->compressed_vis = NULL;
			else
				out->compressed_vis = loadmodel->visdata + p;
			out->efrags = NULL;

			for (j = 0; j < 4; j++)
				out->ambient_sound_level[j] = in->ambient_level[j];

			// gl underwater warp
			if (out->contents != CONTENTS_EMPTY)
			{
				for (j = 0; j < out->nummarksurfaces; j++)
					out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
			}
		}
	}
	else
	{
		dsleaf_t		*in = (dsleaf_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), "leafs");

		loadmodel->leafs = out;
		loadmodel->numleafs = count;

		for (i = 0; i < count; i++, in++, out++)
		{
			for (j = 0; j < 3; j++)
			{
				out->minmaxs[j] = LittleShort (in->mins[j]);
				out->minmaxs[3+j] = LittleShort (in->maxs[j]);
			}

			p = LittleLong(in->contents);
			out->contents = p;

			out->firstmarksurface = loadmodel->marksurfaces + LittleShort(in->firstmarksurface);
			out->nummarksurfaces = LittleShort(in->nummarksurfaces);

			p = LittleLong(in->visofs);
			if (p == -1)
				out->compressed_vis = NULL;
			else
				out->compressed_vis = loadmodel->visdata + p;
			out->efrags = NULL;

			for (j = 0; j < 4; j++)
				out->ambient_sound_level[j] = in->ambient_level[j];

			// gl underwater warp
			if (out->contents != CONTENTS_EMPTY)
			{
				for (j = 0; j < out->nummarksurfaces; j++)
					out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
			}
		}
	}
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes (lump_t *l, qboolean lm)
{
	dsclipnode_t *ins;
	dlclipnode_t *inl;

	mclipnode_t	*out;
	int			i, count;
	hull_t		*hull;

	if (lm)
	{
		ins = NULL;
		inl = (dlclipnode_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*inl))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*inl);
	}
	else
	{
		ins = (dsclipnode_t *)(mod_base + l->fileofs);
		inl = NULL;
		if (l->filelen % sizeof(*ins))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*ins);
	}

	out = (mclipnode_t *)Hunk_AllocName(count * sizeof(*out), "clipnodes");

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

//player
	hull = &loadmodel->hulls[1];
	hull->clipnodes = loadmodel->clipnodes;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

//scorpion
	hull = &loadmodel->hulls[2];
	hull->clipnodes = loadmodel->clipnodes;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -24;
	hull->clip_mins[1] = -24;
	hull->clip_mins[2] = -20;
	hull->clip_maxs[0] = 24;
	hull->clip_maxs[1] = 24;
	hull->clip_maxs[2] = 20;

//crouch
	hull = &loadmodel->hulls[3];
	hull->clipnodes = loadmodel->clipnodes;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -12;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 16;

//hydra -changing in MP to '-8 -8 -8', '8 8 8' for pentacles
	hull = &loadmodel->hulls[4];
	hull->clipnodes = loadmodel->clipnodes;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
#if 0
	hull->clip_mins[0] = -40;
	hull->clip_mins[1] = -40;
	hull->clip_mins[2] = -42;
	hull->clip_maxs[0] = 40;
	hull->clip_maxs[1] = 40;
	hull->clip_maxs[2] = 42;
#else
	hull->clip_mins[0] = -8;
	hull->clip_mins[1] = -8;
	hull->clip_mins[2] = -8;
	hull->clip_maxs[0] = 8;
	hull->clip_maxs[1] = 8;
	hull->clip_maxs[2] = 8;
#endif

//golem - maybe change to '-28 -28 -40', '28 28 40' for Yakman
	hull = &loadmodel->hulls[5];
	hull->clipnodes = loadmodel->clipnodes;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
#if 0	//use yak sizes
	hull->clip_mins[0] = -28;
	hull->clip_mins[1] = -28;
	hull->clip_mins[2] = -40;
	hull->clip_maxs[0] = 28;
	hull->clip_maxs[1] = 28;
	hull->clip_maxs[2] = 40;
#else
	hull->clip_mins[0] = -48;
	hull->clip_mins[1] = -48;
	hull->clip_mins[2] = -50;
	hull->clip_maxs[0] = 48;
	hull->clip_maxs[1] = 48;
	hull->clip_maxs[2] = 50;
#endif

	if (lm)
	{
		for (i = 0; i < count; i++, out++, inl++)
		{
			out->planenum = LittleLong(inl->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			out->children[0] = LittleLong(inl->children[0]);
			out->children[1] = LittleLong(inl->children[1]);
			//Spike: FIXME: bounds check
		}
	}
	else
	{
		for (i = 0; i < count; i++, out++, ins++)
		{
			out->planenum = LittleLong(ins->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

				//johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)LittleShort(ins->children[0]);
			out->children[1] = (unsigned short)LittleShort(ins->children[1]);

			if (out->children[0] >= count)
				out->children[0] -= 65536;
			if (out->children[1] >= count)
				out->children[1] -= 65536;
			//johnfitz
		}
	}

}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	mclipnode_t	*out;
	int			i, j, count;
	hull_t		*hull;

	hull = &loadmodel->hulls[0];

	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = (mclipnode_t *) Hunk_AllocName (count * sizeof(*out), "hull0");

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j = 0; j < 2; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
static void Mod_LoadMarksurfaces (lump_t *l, qboolean lm)
{
	int		i, j, count;
	msurface_t	**out;

	if (lm)
	{
		int		*in = (int *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (msurface_t **) Hunk_AllocName (count * sizeof(*out), "marksurfaces");

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		for (i = 0; i < count; i++)
		{
			j = LittleLong(in[i]);
			if (j >= loadmodel->numsurfaces)
				Sys_Error ("%s: bad surface number", __thisfunc__);
			out[i] = loadmodel->surfaces + j;
		}
	}
	else
	{
		unsigned short		*in = (unsigned short *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*in))
			Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
		count = l->filelen / sizeof(*in);
		out = (msurface_t **) Hunk_AllocName (count * sizeof(*out), "marksurfaces");

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		for (i = 0; i < count; i++)
		{
			j = (unsigned short)LittleShort(in[i]);
			if (j >= loadmodel->numsurfaces)
				Sys_Error ("%s: bad surface number", __thisfunc__);
			out[i] = loadmodel->surfaces + j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
static void Mod_LoadSurfedges (lump_t *l)
{
	int		i, count;
	int		*in, *out;

	in = (int *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (int *) Hunk_AllocName (count * sizeof(*out), "surfedges");

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for (i = 0; i < count; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t	*in;
	int			count;
	int			bits;

	in = (dplane_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("%s: funny lump size in %s", __thisfunc__, loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mplane_t *) Hunk_AllocName (count * 2 * sizeof(*out), "planes");

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		bits = 0;
		for (j = 0; j < 3; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
static float RadiusFromBounds (const vec3_t mins, const vec3_t maxs)
{
	int		i;
	vec3_t	corner;
	vec_t	a, b;

	for (i = 0; i < 3; i++)
	{
		b = fabs(maxs[i]);
		a = fabs(mins[i]);
		corner[i] = (a > b)? a : b;
	}

	return VectorLength (corner);
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer)
{
	int			i, j;
	dheader_t	*header;
	dmodel_t	*bm;
	qboolean lm = false;

	loadmodel->type = mod_brush;

	header = (dheader_t *)buffer;

	i = LittleLong (header->version);
	if (i == BSP2VERSION)
		lm = true;
	else if (i != BSPVERSION)
		Sys_Error ("%s: %s has wrong version number (%i should be %i or %i)", __thisfunc__, mod->name, i, BSPVERSION, BSP2VERSION);

	mod_bsp2 = lm;

// swap all the lumps
	mod_base = (byte *)header;

	for (i = 0; i < (int) sizeof(dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap

	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES], lm);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES], lm);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES], lm);
	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS], lm);
	Mod_LoadNodes (&header->lumps[LUMP_NODES], lm);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES], lm);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 ();

	mod->numframes = 2;		// regular and alternate animation

//
// set up the submodels (FIXME: this is confusing)
//
	for (i = 0; i < mod->numsubmodels; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j = 1; j < MAX_MAP_HULLS; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		mod->radius = RadiusFromBounds (mod->mins, mod->maxs);

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[10];

			q_snprintf (name, sizeof(name), "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts[MAXALIASFRAMES];
static int		posenum;

byte		player_8bit_texels[MAX_PLAYER_CLASS][620*245];

static float	aliastransform[3][4];

static void R_AliasTransformVector (vec3_t in, vec3_t out)
{
	out[0] = DotProduct(in, aliastransform[0]) + aliastransform[0][3];
	out[1] = DotProduct(in, aliastransform[1]) + aliastransform[1][3];
	out[2] = DotProduct(in, aliastransform[2]) + aliastransform[2][3];
}

/*
=================
Mod_LoadAliasFrame
=================
*/
static void *Mod_LoadAliasFrame (void *pin, maliasframedesc_t *frame)
{
	trivertx_t	*pinframe;
	int		i, j;
	daliasframe_t	*pdaliasframe;
	vec3_t		in, out;

	pdaliasframe = (daliasframe_t *)pin;

	strcpy (frame->name, pdaliasframe->name);
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i = 0; i < 3; i++)
	{
	// these are byte values, so we don't have to worry about
	// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	aliastransform[0][0] = pheader->scale[0];
	aliastransform[1][1] = pheader->scale[1];
	aliastransform[2][2] = pheader->scale[2];
	aliastransform[0][3] = pheader->scale_origin[0];
	aliastransform[1][3] = pheader->scale_origin[1];
	aliastransform[2][3] = pheader->scale_origin[2];

	for (j = 0; j < pheader->numverts; j++)
	{
		in[0] = pinframe[j].v[0];
		in[1] = pinframe[j].v[1];
		in[2] = pinframe[j].v[2];
		R_AliasTransformVector(in, out);
		for (i = 0; i < 3; i++)
		{
			if (aliasmins[i] > out[i])
				aliasmins[i] = out[i];
			if (aliasmaxs[i] < out[i])
				aliasmaxs[i] = out[i];
		}
	}
	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
static void *Mod_LoadAliasGroup (void *pin,  maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int			i, j, k, numframes;
	daliasinterval_t	*pin_intervals;
	void			*ptemp;
	vec3_t			in, out;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i = 0; i < 3; i++)
	{
	// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	aliastransform[0][0] = pheader->scale[0];
	aliastransform[1][1] = pheader->scale[1];
	aliastransform[2][2] = pheader->scale[2];
	aliastransform[0][3] = pheader->scale_origin[0];
	aliastransform[1][3] = pheader->scale_origin[1];
	aliastransform[2][3] = pheader->scale_origin[2];

	for (i = 0; i < numframes; i++)
	{
		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);

		for (j = 0; j < pheader->numverts; j++)
		{
			in[0] = poseverts[posenum][j].v[0];
			in[1] = poseverts[posenum][j].v[1];
			in[2] = poseverts[posenum][j].v[2];
			R_AliasTransformVector(in, out);
			for (k = 0; k < 3; k++)
			{
				if (aliasmins[k] > out[k])
					aliasmins[k] = out[k];
				if (aliasmaxs[k] < out[k])
					aliasmaxs[k] = out[k];
			}
		}

		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define	FLOODFILL_FIFO_SIZE		0x1000
#define	FLOODFILL_FIFO_MASK		(FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy )				\
do {								\
	if (pos[(off)] == fillcolor)				\
	{							\
		pos[(off)] = 255;				\
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;	\
	}							\
	else if (pos[(off)] != 255)				\
		fdc = pos[(off)];				\
} while (0)

static void Mod_FloodFillSkin (byte *skin, int skinwidth, int skinheight)
{
	byte		fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t	fifo[FLOODFILL_FIFO_SIZE];
	int			inpt = 0, outpt = 0;
	int			filledcolor = -1;
	int			i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
		{
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
		}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int		x = fifo[outpt].x, y = fifo[outpt].y;
		int		fdc = filledcolor;
		byte	*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)
		{
			FLOODFILL_STEP( -1, -1, 0 );
		}
		if (x < skinwidth - 1)
		{
			FLOODFILL_STEP( 1, 1, 0 );
		}
		if (y > 0)
		{
			FLOODFILL_STEP( -skinwidth, 0, -1 );
		}
		if (y < skinheight - 1)
		{
			FLOODFILL_STEP( skinwidth, 0, 1 );
		}
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
static void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype, int mdl_flags)
{
	int	i, j, k;
	char	name[MAX_QPATH];
	int	s;
	byte	*skin;
	int	tex_mode;
	int	groupskins;
	daliasskingroup_t	*pinskingroup;
	daliasskininterval_t	*pinskinintervals;
	src_offset_t		offset; //johnfitz

	skin = (byte *)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("%s: Invalid # of skins: %d", __thisfunc__, numskins);

	s = pheader->skinwidth * pheader->skinheight;

	tex_mode = TEXPREF_NONE | TEXPREF_MIPMAP;
	if (mdl_flags & EF_TRANSPARENT)
		tex_mode |= TEXPREF_TRANSPARENT;
	else if (mdl_flags & EF_HOLEY)
		tex_mode |= TEX_HOLEY;
	else if (mdl_flags & EF_SPECIAL_TRANS)
		tex_mode |= TEX_SPECIAL_TRANS;

	for (i = 0; i < numskins; i++)
	{
	    k = LittleLong (pskintype->type);		/* aliasskintype_t */
	    if (k == ALIAS_SKIN_SINGLE) {
		Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);

		// save 8 bit texels for the player model to remap
		if (!strcmp(loadmodel->name,"models/paladin.mdl"))
		{
			if (s > (int) sizeof(player_8bit_texels[0]))
				goto skin_too_large;
			memcpy (player_8bit_texels[0], (byte *)(pskintype + 1), s);
		}
		else if (!strcmp(loadmodel->name,"models/crusader.mdl"))
		{
			if (s > (int) sizeof(player_8bit_texels[1]))
				goto skin_too_large;
			memcpy (player_8bit_texels[1], (byte *)(pskintype + 1), s);
		}
		else if (!strcmp(loadmodel->name,"models/necro.mdl"))
		{
			if (s > (int) sizeof(player_8bit_texels[2]))
				goto skin_too_large;
			memcpy (player_8bit_texels[2], (byte *)(pskintype + 1), s);
		}
		else if (!strcmp(loadmodel->name,"models/assassin.mdl"))
		{
			if (s > (int) sizeof(player_8bit_texels[3]))
				goto skin_too_large;
			memcpy (player_8bit_texels[3], (byte *)(pskintype + 1), s);
		}
		else if (!strcmp(loadmodel->name,"models/succubus.mdl"))
		{
			if (s > (int) sizeof(player_8bit_texels[4]))
				goto skin_too_large;
			memcpy (player_8bit_texels[4], (byte *)(pskintype + 1), s);
		}

		offset = (src_offset_t)(pskintype + 1) - (src_offset_t)mod_base;
		q_snprintf (name, sizeof(name), "%s_%i", loadmodel->name, i);
		pheader->gltextures[i][0] =
		pheader->gltextures[i][1] =
		pheader->gltextures[i][2] =
		pheader->gltextures[i][3] = TexMgr_LoadImage(loadmodel, name, pheader->skinwidth, pheader->skinheight,
			SRC_INDEXED, (byte *)(pskintype + 1), loadmodel->name, offset, tex_mode | TEXPREF_NOBRIGHT);
			
			//GL_LoadTexture (name, (byte *)(pskintype + 1),
			//pheader->skinwidth, pheader->skinheight, tex_mode);
		pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + s);

	    } else /*if (k == ALIAS_SKIN_GROUP)*/
	    {						/* animating skin group.  yuck. */
		pskintype++;
		pinskingroup = (daliasskingroup_t *)pskintype;
		groupskins = LittleLong (pinskingroup->numskins);
		pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

		pskintype = (daliasskintype_t *)(pinskinintervals + groupskins);
		for (j = 0; j < groupskins; j++)
		{
			Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);
			offset = (src_offset_t)(pskintype + 1) - (src_offset_t)mod_base;
			q_snprintf (name, sizeof(name), "%s_%i_%i", loadmodel->name, i, j);
			pheader->gltextures[i][j&3] = TexMgr_LoadImage(loadmodel, name, pheader->skinwidth, pheader->skinheight,
				SRC_INDEXED, (byte *)(pskintype + 1), loadmodel->name, offset, tex_mode | TEXPREF_NOBRIGHT);
				
				//GL_LoadTexture (name, (byte *)(pskintype),
				//	pheader->skinwidth, pheader->skinheight, tex_mode);
			pskintype = (daliasskintype_t *)((byte *)(pskintype) + s);
		}
		for (k = j; j < 4; j++)
			pheader->gltextures[i][j&3] = pheader->gltextures[i][j - k];
	    }
	}

	return (void *)pskintype;

skin_too_large:
	Sys_Error ("Player skin too large");
	return NULL;
}

//=========================================================================

static void Mod_SetAliasModelExtraFlags (qmodel_t *mod)
{
	mod->ex_flags = 0;

	// Torch glows
	if (!q_strncasecmp (mod->name, "models/rflmtrch",15) ||
	    !q_strncasecmp (mod->name, "models/cflmtrch",15) ||
	    !q_strncasecmp (mod->name, "models/castrch",15)  ||
	    !q_strncasecmp (mod->name, "models/rometrch",15) ||
	    !q_strncasecmp (mod->name, "models/egtorch",14)  ||
	    !q_strncasecmp (mod->name, "models/flame",12))
	{
		mod->ex_flags |= XF_TORCH_GLOW;
		// set yellow color
		mod->glow_color[0] = 0.8f;
		mod->glow_color[1] = 0.4f;
		mod->glow_color[2] = 0.1f;
		mod->glow_color[3] = 1.0f;
	}
	else if (!q_strncasecmp (mod->name, "models/eflmtrch",15))
	{
		mod->ex_flags |= (XF_TORCH_GLOW | XF_TORCH_GLOW_EGYPT);
		mod->glow_color[0] = 0.8f;
		mod->glow_color[1] = 0.4f;
		mod->glow_color[2] = 0.1f;
		mod->glow_color[3] = 1.0f;
	}
	// Mana glows
	else if (!q_strncasecmp (mod->name, "models/i_bmana",14))
	{
		mod->ex_flags |= XF_GLOW;
		mod->glow_color[0] = 0.25f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 1.0f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/i_gmana",14))
	{
		mod->ex_flags |= XF_GLOW;
		mod->glow_color[0] = 0.25f;
		mod->glow_color[1] = 1.0f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/i_btmana",15))
	{
		mod->ex_flags |= XF_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	// Missile glows
	else if (!q_strncasecmp (mod->name, "models/drgnball",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/eidoball",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.55f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/lavaball",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/glowball",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/fireball",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/famshot",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.2f;
		mod->glow_color[1] = 0.8f;
		mod->glow_color[2] = 0.2f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/pestshot",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.2f;
		mod->glow_color[1] = 0.2f;
		mod->glow_color[2] = 0.2f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/mumshot",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/scrbstp1",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.05f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/scrbpbody",16))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.05f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/iceshot2",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.25f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 1.0f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/iceshot",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.25f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 1.0f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/flaming",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.55f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/sucwp1p",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 8.0f;
		mod->glow_color[1] = 0.2f;
		mod->glow_color[2] = 0.2f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/sucwp2p",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.2f;
		mod->glow_color[1] = 0.8f;
		mod->glow_color[2] = 0.2f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/goop",11))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.8f;
		mod->glow_color[2] = 0.2f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/purfir1",14))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.75f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/golemmis",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/shard",12) && strlen(mod->name) == 12)
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/shardice",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.25f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 1.0f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/snakearr",15))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 0.25f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/spit",11))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 0.25f;
		mod->glow_color[1] = 1.0f;
		mod->glow_color[2] = 0.25f;
		mod->glow_color[3] = 0.5f;
	}
	else if (!q_strncasecmp (mod->name, "models/spike",12))
	{
		mod->ex_flags |= XF_MISSILE_GLOW;
		mod->glow_color[0] = 1.0f;
		mod->glow_color[1] = 1.0f;
		mod->glow_color[2] = 1.0f;
		mod->glow_color[3] = 0.5f;
	}
}


/*
=================
Mod_LoadAliasModelNew
reads extra field for num ST verts, and extra index list of them
=================
*/
static void Mod_LoadAliasModelNew (qmodel_t *mod, void *buffer)
{
	int			i, j;
	newmdl_t		*pinmodel;
	stvert_t		*pinstverts;
	dnewtriangle_t		*pintriangles;
	int			version, numframes;
	int			size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int			start, end, total;

	Mod_SetAliasModelExtraFlags (mod);

	start = Hunk_LowMark ();

	pinmodel = (newmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_NEWVERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_NEWVERSION);

//	Con_Printf("Loading NEW model %s\n",mod->name);
//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t) 
			+ (LittleLong (pinmodel->numframes) - 1) * sizeof (pheader->frames[0]);
	pheader = (aliashdr_t *) Hunk_AllocName (size, loadname);

	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_SKIN_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name, MAX_SKIN_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);
	pheader->version = LittleLong (pinmodel->num_st_verts);	//hide num_st in version

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("%s: Invalid # of frames: %d", __thisfunc__, numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i = 0; i < 3; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (pheader->numskins, pskintype, mod->flags);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i = 0; i < pheader->version; i++)	//version holds num_st_verts
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dnewtriangle_t *)&pinstverts[pheader->version];

	for (i = 0; i < pheader->numtris; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j = 0; j < 3; j++)
		{
			triangles[i].vertindex[j] = LittleShort (pintriangles[i].vertindex[j]);
			triangles[i].stindex[j]	  = LittleShort (pintriangles[i].stindex[j]);
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	aliasmins[0] = aliasmins[1] = aliasmins[2] = 32768;
	aliasmaxs[0] = aliasmaxs[1] = aliasmaxs[2] = -32768;

	for (i = 0; i < numframes; i++)
	{
		aliasframetype_t	frametype;

		frametype = (aliasframetype_t) LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		}
		else
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
		}
	}

	//Con_Printf("Model is %s\n",mod->name);
	//Con_Printf("   Mins is %5.2f, %5.2f, %5.2f\n",aliasmins[0],aliasmins[1],aliasmins[2]);
	//Con_Printf("   Maxs is %5.2f, %5.2f, %5.2f\n",aliasmaxs[0],aliasmaxs[1],aliasmaxs[2]);

	pheader->numposes = posenum;

	mod->type = mod_alias;

// FIXME: do this right
//	mod->mins[0] = mod->mins[1] = mod->mins[2] = -16;
//	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16;
	mod->mins[0] = aliasmins[0] - 10;
	mod->mins[1] = aliasmins[1] - 10;
	mod->mins[2] = aliasmins[2] - 10;
	mod->maxs[0] = aliasmaxs[0] + 10;
	mod->maxs[1] = aliasmaxs[1] + 10;
	mod->maxs[2] = aliasmaxs[2] + 10;

//
// build the draw lists
//
	GL_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//
	end = Hunk_LowMark ();
	total = end - start;

	if (!mod->cache.data)
		Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

/*
=================
Mod_LoadAliasModel
=================
*/
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer)
{
	int			i, j;
	mdl_t			*pinmodel;
	stvert_t		*pinstverts;
	dtriangle_t		*pintriangles;
	int			version, numframes;
	int			size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;
	int			start, end, total;

	Mod_SetAliasModelExtraFlags (mod);

	start = Hunk_LowMark ();

	pinmodel = (mdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size = 	sizeof (aliashdr_t) 
			+ (LittleLong (pinmodel->numframes) - 1) * sizeof (pheader->frames[0]);
	pheader = (aliashdr_t *) Hunk_AllocName (size, loadname);

	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_SKIN_HEIGHT)
		Sys_Error ("model %s has a skin taller than %d", mod->name, MAX_SKIN_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);
	pheader->version = pheader->numverts;	//hide num_st in version

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices", mod->name);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("%s: Invalid # of frames: %d", __thisfunc__, numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i = 0; i < 3; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (pheader->numskins, pskintype, mod->flags);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i = 0; i < pheader->version; i++)	//version holds num_st_verts
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i = 0; i < pheader->numtris; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j = 0; j < 3; j++)
		{
			triangles[i].vertindex[j] = (unsigned short)LittleLong (pintriangles[i].vertindex[j]);
			triangles[i].stindex[j]	  = triangles[i].vertindex[j];
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	aliasmins[0] = aliasmins[1] = aliasmins[2] = 32768;
	aliasmaxs[0] = aliasmaxs[1] = aliasmaxs[2] = -32768;

	for (i = 0; i < numframes; i++)
	{
		aliasframetype_t	frametype;

		frametype = (aliasframetype_t) LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		}
		else
		{
			pframetype = (daliasframetype_t *)
					Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
		}
	}

	//Con_Printf("Model is %s\n",mod->name);
	//Con_Printf("   Mins is %5.2f, %5.2f, %5.2f\n",aliasmins[0],aliasmins[1],aliasmins[2]);
	//Con_Printf("   Maxs is %5.2f, %5.2f, %5.2f\n",aliasmaxs[0],aliasmaxs[1],aliasmaxs[2]);

	pheader->numposes = posenum;

	mod->type = mod_alias;

// FIXME: do this right
//	mod->mins[0] = mod->mins[1] = mod->mins[2] = -16;
//	mod->maxs[0] = mod->maxs[1] = mod->maxs[2] = 16;

	mod->mins[0] = aliasmins[0] - 10;
	mod->mins[1] = aliasmins[1] - 10;
	mod->mins[2] = aliasmins[2] - 10;
	mod->maxs[0] = aliasmaxs[0] + 10;
	mod->maxs[1] = aliasmaxs[1] + 10;
	mod->maxs[2] = aliasmaxs[2] + 10;

//
// build the draw lists
//
	GL_MakeAliasModelDisplayLists (mod, pheader);

//
// move the complete, relocatable alias model to the cache
//
	end = Hunk_LowMark ();
	total = end - start;

	if (!mod->cache.data)
		Cache_Alloc (&mod->cache, total, loadname);
	if (!mod->cache.data)
		return;
	memcpy (mod->cache.data, pheader, total);

	Hunk_FreeToLowMark (start);
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
static void *Mod_LoadSpriteFrame (void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int			width, height, size, origin[2];
	char			name[MAX_QPATH];
	src_offset_t			offset; //johnfitz

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	if (spr_reload_only && (*ppframe))
	{
		pspriteframe = *ppframe;
	}
	else
	{
		pspriteframe = (mspriteframe_t *) Hunk_AllocName(sizeof(mspriteframe_t), loadname);
		*ppframe = pspriteframe;
	}

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	//johnfitz -- image might be padded
	pspriteframe->smax = (float)width / (float)TexMgr_PadConditional(width);
	pspriteframe->tmax = (float)height / (float)TexMgr_PadConditional(height);
	//johnfitz

	q_snprintf (name, sizeof(name), "%s_%i", loadmodel->name, framenum);
	offset = (src_offset_t)(pinframe + 1) - (src_offset_t)mod_base; //johnfitz
	//pspriteframe->gltexture = GL_LoadTexture (name, (byte *)(pinframe + 1), width, height, TEX_MIPMAP | TEX_ALPHA);
	pspriteframe->gltexture = TexMgr_LoadImage(loadmodel, name, width, height, SRC_INDEXED, (byte *)(pinframe + 1),
		WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP);

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
static void *Mod_LoadSpriteGroup (void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int			i, numframes;
	dspriteinterval_t	*pin_intervals;
	float			*poutintervals;
	void			*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	if (spr_reload_only && (*ppframe))
	{
		pspritegroup = (mspritegroup_t *)(*ppframe);
	}
	else
	{
		pspritegroup = (mspritegroup_t *) Hunk_AllocName(sizeof(mspritegroup_t) + (numframes - 1) * sizeof(pspritegroup->frames[0]), loadname);
		*ppframe = (mspriteframe_t *)pspritegroup;
	}

	pspritegroup->numframes = numframes;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	if (spr_reload_only && pspritegroup->intervals)
	{
		poutintervals = pspritegroup->intervals;
	}
	else
	{
		poutintervals = (float *) Hunk_AllocName (numframes * sizeof (float), loadname);
		pspritegroup->intervals = poutintervals;
	}

	for (i = 0; i < numframes; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("%s: interval <= 0", __thisfunc__);

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i = 0; i < numframes; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer)
{
	int			i;
	int			version;
	dsprite_t		*pin;
	msprite_t		*psprite;
	int			numframes;
	int			size;
	dspriteframetype_t	*pframetype;

	pin = (dsprite_t *)buffer;
	mod_base = (byte *)buffer; //johnfitz

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	// Pa3PyX: if the pointer is set and needload == NL_NEEDS_LOADED,
	// we are just reloading textures, and are already allocated
	if (spr_reload_only && mod->cache.data)
	{
		psprite = (msprite_t *) mod->cache.data;
	}
	else
	{
		psprite = (msprite_t *) Hunk_AllocName (size, loadname);
		mod->cache.data = psprite;
	}

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = (synctype_t) LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;

//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("%s: Invalid # of frames: %d", __thisfunc__, numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i = 0; i < numframes; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1, &psprite->frames[i].frameptr, i);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1, &psprite->frames[i].frameptr, i);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
#if defined(__GNUC__) && !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define MOD_Printf(FH, fmt, args...)		\
    do {					\
	if ((FH)) fprintf((FH), fmt, ##args);	\
	else Con_Printf(fmt, ##args);		\
    } while (0)
#else
#define MOD_Printf(FH, ...)			\
    do {					\
	if ((FH)) fprintf((FH), __VA_ARGS__);	\
	else Con_Printf(__VA_ARGS__);		\
    } while (0)
#endif
static void Mod_Print (void)
{
	int		i, counter;
	FILE		*FH = NULL;
	qmodel_t	*mod;

	i = Cmd_Argc();
	for (counter = 1; counter < i; counter++)
	{
		if (q_strcasecmp(Cmd_Argv(counter),"save") == 0)
		{
			FH = fopen(FS_MakePath(FS_USERDIR,NULL,"mcache.txt"), "w");
			break;
		}
	}

	MOD_Printf (FH, "Cached models:\n");
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		MOD_Printf (FH, "%4i (%8p): %s", i, mod->cache.data, mod->name);
		if (mod->needload & NL_UNREFERENCED)
			MOD_Printf (FH, " (!R)");
		if (mod->needload & NL_NEEDS_LOADED)
			MOD_Printf (FH, " (!P)");
		MOD_Printf (FH, "\n");
	}
	if (FH)
	{
		fclose(FH);
		Con_Printf ("Wrote to mcache.txt\n");
	}
}

