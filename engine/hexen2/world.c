/*
 * world.c -- world query functions
 * $Id$
 *
 * entities never clip against themselves, or their owner
 * line of sight checks trace->crosscontent, but bullets don't
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

typedef struct
{
	vec3_t		boxmins, boxmaxs;// enclose the test object along entire move
	float		*mins, *maxs;	// size of the moving object
	vec3_t		mins2, maxs2;	// size when clipping against mosnters
	float		*start, *end;
	trace_t		trace;
	int			type;
	edict_t		*passedict;
} moveclip_t;

#if	!id386
static int SV_HullPointContents (hull_t *hull, int num, vec3_t p);
#endif


/*
===============================================================================

HULL BOXES

===============================================================================
*/


static	hull_t		box_hull;
static	mclipnode_t	box_clipnodes[6];
static	mplane_t	box_planes[6];
static	int			move_type;

/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
static void SV_InitBoxHull (void)
{
	int		i;
	int		side;

	box_hull.clipnodes = box_clipnodes;
	box_hull.planes = box_planes;
	box_hull.firstclipnode = 0;
	box_hull.lastclipnode = 5;

	for (i = 0; i < 6; i++)
	{
		box_clipnodes[i].planenum = i;

		side = i & 1;

		box_clipnodes[i].children[side] = CONTENTS_EMPTY;
		if (i != 5)
			box_clipnodes[i].children[side^1] = i + 1;
		else
			box_clipnodes[i].children[side^1] = CONTENTS_SOLID;

		box_planes[i].type = i>>1;
		box_planes[i].normal[i>>1] = 1;
	}
}


/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
static hull_t	*SV_HullForBox (vec3_t mins, vec3_t maxs)
{
	box_planes[0].dist = maxs[0];
	box_planes[1].dist = mins[0];
	box_planes[2].dist = maxs[1];
	box_planes[3].dist = mins[1];
	box_planes[4].dist = maxs[2];
	box_planes[5].dist = mins[2];

	return &box_hull;
}


/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
static hull_t *SV_HullForEntity (edict_t *ent, vec3_t mins, vec3_t maxs, vec3_t offset, edict_t *move_ent)
{
	qmodel_t	*model;
	vec3_t		size;
	vec3_t		hullmins, hullmaxs;
	hull_t		*hull;
	int			idx;

	// decide which clipping hull to use, based on the size
	if (ent->v.solid == SOLID_BSP)
	{	// explicit hulls in the BSP model
		if (ent->v.movetype != MOVETYPE_PUSH)
			Sys_Error ("SOLID_BSP without MOVETYPE_PUSH");

		model = sv.models[ (int)ent->v.modelindex ];

		if (!model || model->type != mod_brush)
			Sys_Error ("SOLID_BSP with a non bsp model");

		VectorSubtract (maxs, mins, size);

		//THIS IS WHERE THE MONSTER STEPPING ERROR WAS- IN CHECKBOTTOM,
		//A '0 0 0' MINS AND MAXS WERE SENT, BUT HERE, IT LOOKS TO SEE
		//IF THE MONSTER HAS A HULL AND CALCULATES THE OFFSET FROM
		//THE HULL MINS AND MAXS AND THE PASSED MINS AND MAXS,
		//THIS WILL INCORRECTLY OFFSET THE TEST MOVE BY THE MINS AND
		//MAXS OF THE MONSTER!  WILL CHECK FOR SIDE EFFECTS...

		if (move_ent->v.hull)	// Entity is specifying which hull to use
		{
			idx = move_ent->v.hull-1;
			hull = &model->hulls[idx];
			if (!hull)  // Invalid hull
			{
				Con_Printf ("ERROR: hull %d is null.\n", idx);
				hull = &model->hulls[0];
			}
		}
		else	// Using the old way uses size to determine hull to use
		{
		//	if ((int)move_ent->v.flags & FL_MONSTER)
		//		Con_DPrintf("ERROR: auto-detecing hull for monster!\n");
			if (size[0] < 3) // Point
				hull = &model->hulls[0];
			else if ( (size[0] <= 8) && ((int)(sv.edicts->v.spawnflags) & 1) )  // Pentacles
				hull = &model->hulls[4];
			else if (size[0] <= 32 && size[2] <= 28) // Half Player
				hull = &model->hulls[3];
			else if (size[0] <= 32) // Full Player
				hull = &model->hulls[1];
			else // Golem
				hull = &model->hulls[5];
		}

	// calculate an offset value to center the origin:
		VectorSubtract (hull->clip_mins, mins, offset);
		if ((int)move_ent->v.flags & FL_MONSTER)
		{
			if (offset[0] != 0 || offset[1] != 0)
			{
			//	Con_DPrintf("ERROR: Non-zero offset (%f,%f,%f)!!!\n",
			//			offset[0], offset[1], offset[2]);
			// 524288 (FL_MISMATCHEDBOUNDS ?) is an abandoned H2MP flag?
			// if ((int)move_ent->v.flags2 & 524288)
				offset[0] = 0;
				offset[1] = 0;
			}
		}
		VectorAdd (offset, ent->v.origin, offset);
	}
	else
	{	// create a temp hull from bounding box sizes
		VectorSubtract (ent->v.mins, maxs, hullmins);
		VectorSubtract (ent->v.maxs, mins, hullmaxs);
		hull = SV_HullForBox (hullmins, hullmaxs);

		VectorCopy (ent->v.origin, offset);
	}

	return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

static	areanode_t	sv_areanodes[AREA_NODES];
static	int			areanodedepth;
static	int			sv_numareanodes;

/*
===============
SV_CreateAreaNode

===============
*/
static areanode_t *SV_CreateAreaNode (int depth, vec3_t mins, vec3_t maxs)
{
	areanode_t	*anode;
	vec3_t		size;
	vec3_t		mins1, maxs1, mins2, maxs2;

	anode = &sv_areanodes[sv_numareanodes];
	sv_numareanodes++;

	ClearLink (&anode->trigger_edicts);
	ClearLink (&anode->solid_edicts);

	VectorSubtract(maxs, mins, size);

	if (depth == AREA_DEPTH || (size[0] < 512 && size[1] < 512))
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}

	if (size[0] > size[1])
		anode->axis = 0;
	else
		anode->axis = 1;

	anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
	VectorCopy (mins, mins1);
	VectorCopy (mins, mins2);
	VectorCopy (maxs, maxs1);
	VectorCopy (maxs, maxs2);

	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

	anode->children[0] = SV_CreateAreaNode (depth+1, mins2, maxs2);
	anode->children[1] = SV_CreateAreaNode (depth+1, mins1, maxs1);

	return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld (void)
{
	SV_InitBoxHull ();

	memset (sv_areanodes, 0, sizeof(sv_areanodes));
	sv_numareanodes = 0;
	SV_CreateAreaNode (0, sv.worldmodel->mins, sv.worldmodel->maxs);
	areanodedepth = 0;
}


/*
===============
SV_UnlinkEdict

===============
*/
static link_t	**sv_link_next;
static link_t	**sv_link_prev;

void SV_UnlinkEdict (edict_t *ent)
{
	if (!ent->area.prev)
		return;		// not linked in anywhere
	RemoveLink (&ent->area);
	if (sv_link_next && *sv_link_next == &ent->area)
		*sv_link_next = ent->area.next;
	if (sv_link_prev && *sv_link_prev == &ent->area)
		*sv_link_prev = ent->area.prev;
	ent->area.prev = ent->area.next = NULL;
}

/*
====================
SV_AreaTriggerEdicts

Spike -- just builds a list of entities within the area, rather than walking
them and risking the list getting corrupt.
====================
*/
static void
SV_AreaTriggerEdicts(edict_t *ent, areanode_t *node, edict_t **list, int *listcount, const int listspace)
{
	link_t		*l, *next;
	edict_t		*touch;

	// touch linked edicts
	for (l = node->trigger_edicts.next; l != &node->trigger_edicts; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA(l);
		if (touch == ent)
			continue;
		if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
			continue;
		if (ent->v.absmin[0] > touch->v.absmax[0]
			|| ent->v.absmin[1] > touch->v.absmax[1]
			|| ent->v.absmin[2] > touch->v.absmax[2]
			|| ent->v.absmax[0] < touch->v.absmin[0]
			|| ent->v.absmax[1] < touch->v.absmin[1]
			|| ent->v.absmax[2] < touch->v.absmin[2])
			continue;

		if (*listcount == listspace)
			return; // should never happen

		list[*listcount] = touch;
		(*listcount)++;
	}

	// recurse down both sides
	if (node->axis == -1)
		return;

	if (ent->v.absmax[node->axis] > node->dist)
		SV_AreaTriggerEdicts(ent, node->children[0], list, listcount, listspace);
	if (ent->v.absmin[node->axis] < node->dist)
		SV_AreaTriggerEdicts(ent, node->children[1], list, listcount, listspace);
}

/*
====================
SV_TouchLinks

ericw -- copy the touching edicts to an array (on the hunk) so we can avoid
iteating the trigger_edicts linked list while calling PR_ExecuteProgram
which could potentially corrupt the list while it's being iterated.
Based on code from Spike.
====================
*/
void SV_TouchLinks(edict_t *ent)
{
	edict_t		**list;
	edict_t		*touch;
	int		old_self, old_other;
	int		i, listcount;
	int		mark;

	mark = Hunk_LowMark();
	list = (edict_t **)Hunk_Alloc(sv.num_edicts * sizeof(edict_t *));

	listcount = 0;
	SV_AreaTriggerEdicts(ent, sv_areanodes, list, &listcount, sv.num_edicts);

	for (i = 0; i < listcount; i++)
	{
		touch = list[i];
		// re-validate in case of PR_ExecuteProgram having side effects that make
		// edicts later in the list no longer touch
		if (touch == ent)
			continue;
		if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
			continue;
		if (ent->v.absmin[0] > touch->v.absmax[0]
			|| ent->v.absmin[1] > touch->v.absmax[1]
			|| ent->v.absmin[2] > touch->v.absmax[2]
			|| ent->v.absmax[0] < touch->v.absmin[0]
			|| ent->v.absmax[1] < touch->v.absmin[1]
			|| ent->v.absmax[2] < touch->v.absmin[2])
			continue;
		old_self = *sv_globals.self;
		old_other = *sv_globals.other;

		*sv_globals.self = EDICT_TO_PROG(touch);
		*sv_globals.other = EDICT_TO_PROG(ent);
		*sv_globals.time = sv.time;
		PR_ExecuteProgram(touch->v.touch);

		*sv_globals.self = old_self;
		*sv_globals.other = old_other;
	}

	// free hunk-allocated edicts array
	Hunk_FreeToLowMark(mark);
}


/*
===============
SV_FindTouchedLeafs

===============
*/
static void SV_FindTouchedLeafs (edict_t *ent, mnode_t *node)
{
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;
	int			leafnum;

loc0:
	if (node->contents == CONTENTS_SOLID)
		return;

	// add an efrag if the node is a leaf
	// statics in walls cause efrag overflow, comment out to show over-culled ents in this case - shan
	if (node->contents < 0)
	{
		if (ent->num_leafs == MAX_ENT_LEAFS)
			return;

		leaf = (mleaf_t *)node;
		leafnum = leaf - sv.worldmodel->leafs - 1;

		ent->leafnums[ent->num_leafs] = leafnum;
		ent->num_leafs++;
		return;
	}

	// NODE_MIXED

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);

	// recurse down the contacted sides
	// LordHavoc: optimized recursion
	//if (sides & 1) SV_FindTouchedLeafs (ent, node->children[0]);
	//if (sides & 2) SV_FindTouchedLeafs (ent, node->children[1]);
	switch (sides)
	{
	case 1:
		node = node->children[0];
		goto loc0;
	case 2:
		node = node->children[1];
		goto loc0;
	default: // 3
		if (node->children[0]->contents != CONTENTS_SOLID)
			SV_FindTouchedLeafs (ent, node->children[0]);
		node = node->children[1];
		goto loc0;
	}
}

/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict (edict_t *ent, qboolean touch_triggers)
{
	areanode_t	*node;

	if (ent->area.prev)
		SV_UnlinkEdict (ent);	// unlink from old position

	if (ent == sv.edicts)
		return;		// don't add the world

	if (ent->free)
		return;

	// set the abs box
	if (ent->v.solid == SOLID_BSP && 
		(ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
	{	// expand for rotation
		float		v, maxv;
		int			i;

		maxv = 0;
		for (i = 0; i < 3; i++)
		{
			v = fabs(ent->v.mins[i]);
			if (v > maxv)
				maxv = v;
			v = fabs(ent->v.maxs[i]);
			if (v > maxv)
				maxv = v;
		}
		for (i = 0; i < 3; i++)
		{
			ent->v.absmin[i] = ent->v.origin[i] - maxv;
			ent->v.absmax[i] = ent->v.origin[i] + maxv;
		}
	}
	else
	{
		VectorAdd (ent->v.origin, ent->v.mins, ent->v.absmin);
		VectorAdd (ent->v.origin, ent->v.maxs, ent->v.absmax);
	}

//
// to make items easier to pick up and allow them to be grabbed off
// of shelves, the abs sizes are expanded
//
	if ((int)ent->v.flags & FL_ITEM)
	{
		ent->v.absmin[0] -= 15;
		ent->v.absmin[1] -= 15;
		ent->v.absmax[0] += 15;
		ent->v.absmax[1] += 15;
	}
	else
	{	// because movement is clipped an epsilon away from an actual edge,
		// we must fully check even when bounding boxes don't quite touch
		ent->v.absmin[0] -= 1;
		ent->v.absmin[1] -= 1;
		ent->v.absmin[2] -= 1;
		ent->v.absmax[0] += 1;
		ent->v.absmax[1] += 1;
		ent->v.absmax[2] += 1;
	}

	// link to PVS leafs
	ent->num_leafs = 0;
	if (ent->v.modelindex)
		SV_FindTouchedLeafs (ent, sv.worldmodel->nodes);

	if (ent->v.solid == SOLID_NOT)
		return;

	// find the first node that the ent's box crosses
	node = sv_areanodes;
	while (1)
	{
		if (node->axis == -1)
			break;
		if (ent->v.absmin[node->axis] > node->dist)
			node = node->children[0];
		else if (ent->v.absmax[node->axis] < node->dist)
			node = node->children[1];
		else
			break;		// crosses the node
	}

	// link it in

	if (ent->v.solid == SOLID_TRIGGER)
		InsertLinkBefore (&ent->area, &node->trigger_edicts);
	else
		InsertLinkBefore (&ent->area, &node->solid_edicts);

	// if touch_triggers, touch all entities at this node and decend for more
	if (touch_triggers)
		SV_TouchLinks ( ent );
}


/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

#if	!id386

/*
==================
SV_HullPointContents

==================
*/
static int SV_HullPointContents (hull_t *hull, int num, vec3_t p)
{
	float		d;
	mclipnode_t	*node;
	mplane_t	*plane;

	while (num >= 0)
	{
		if (num < hull->firstclipnode || num > hull->lastclipnode)
			Sys_Error ("%s: bad node number", __thisfunc__);

		node = hull->clipnodes + num;
		plane = hull->planes + node->planenum;

		if (plane->type < 3)
			d = p[plane->type] - plane->dist;
		else
			d = DotProductDBL(plane->normal, p) - plane->dist;
		if (d < 0)
			num = node->children[1];
		else
			num = node->children[0];
	}

	return num;
}

#endif	/* !id386 */


/*
==================
SV_PointContents

==================
*/
int SV_PointContents (vec3_t p)
{
	int		cont;

	cont = SV_HullPointContents (&sv.worldmodel->hulls[0], 0, p);
	if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN)
		cont = CONTENTS_WATER;
	return cont;
}

#ifdef QUAKE2
int SV_TruePointContents (vec3_t p)
{
	return SV_HullPointContents (&sv.worldmodel->hulls[0], 0, p);
}
#endif

//===========================================================================

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
FIXME!!!  For rotating doors, this is totally inaccurate
============
*/
edict_t	*SV_TestEntityPosition (edict_t *ent)
{
	trace_t	trace;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent);

	if (trace.startsolid)
	{
	//	Con_DPrintf("%s inside check\n", PR_GetString(trace.ent->v.classname));
		return sv.edicts;
	}

	return NULL;
}


/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

static void WackyBugFixer(float *p1, float *p2, float *frac, float *mid)
{
	int i;

	for (i = 0; i < 3; i++)
		mid[i] = p1[i] + (*frac)*(p2[i] - p1[i]);
}

/*
==================
SV_RecursiveHullCheck

==================
*/
qboolean SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	mclipnode_t	*node;
	mplane_t	*plane;
	float		t1, t2;
	float		frac;
	int			i;
	vec3_t		mid;
	int			side;
	float		midf;
	int			contents;

loc0: // optimized recursion

// check for empty
	if (num < 0)
	{
		if (num != CONTENTS_SOLID)
		{
			trace->allsolid = false;
			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		}
		else
			trace->startsolid = true;

		return true;		// empty
	}

	if (num < hull->firstclipnode || num > hull->lastclipnode)
		Sys_Error ("%s: bad node number", __thisfunc__);

//
// find the point distances
//
	node = hull->clipnodes + num;
	plane = hull->planes + node->planenum;

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
	}
	else
	{
		t1 = DotProductDBL(plane->normal, p1) - plane->dist;
		t2 = DotProductDBL(plane->normal, p2) - plane->dist;
	}

#if 1
	if (t1 >= 0 && t2 >= 0)
	{
		//return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
		num = node->children[0];
		goto loc0;
	}
	if (t1 < 0 && t2 < 0)
	{
		//return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
		num = node->children[1];
		goto loc0;
	}
#else
	if ( (t1 >= DIST_EPSILON && t2 >= DIST_EPSILON) || (t2 > t1 && t1 >= 0) )
		return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if ( (t1 <= -DIST_EPSILON && t2 <= -DIST_EPSILON) || (t2 < t1 && t1 <= 0) )
		return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
#endif

// put the crosspoint DIST_EPSILON pixels on the near side
	if (t1 < 0)
		frac = (t1 + DIST_EPSILON)/(t1-t2);
	else
		frac = (t1 - DIST_EPSILON)/(t1-t2);
	if (frac < 0)
		frac = 0;
	else if (frac > 1)
		frac = 1;

	midf = p1f + (p2f - p1f)*frac;
	for (i = 0; i < 3; i++)
		mid[i] = p1[i] + frac*(p2[i] - p1[i]);

	side = (t1 < 0);

// move up to the node
	if (!SV_RecursiveHullCheck (hull, node->children[side], p1f, midf, p1, mid, trace) )
		return false;
#ifdef PARANOID
	if (SV_HullPointContents (hull, node->children[side], mid) == CONTENTS_SOLID)
	{
		Con_Printf ("mid PointInHullSolid\n");
		return false;
	}
#endif

	// LordHavoc: this recursion can not be optimized because mid would need to be duplicated on a stack
	contents = SV_HullPointContents (hull, node->children[side^1], mid);
//	if (contents != CONTENTS_SOLID && (contents == CONTENTS_WATER || move_type != MOVE_WATER))
	if (contents != CONTENTS_SOLID)
		// go past the node
		return SV_RecursiveHullCheck (hull, node->children[side^1], midf, p2f, mid, p2, trace);

	if (trace->allsolid)
		return false;		// never got out of the solid area

//==================
// the other side of the node is solid, this is the impact point
//==================
	if (!side)
	{
		VectorCopy (plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
	}
	else
	{
		VectorNegate (plane->normal, trace->plane.normal);
		trace->plane.dist = -plane->dist;
	}

//	while (SV_HullPointContents (hull, hull->firstclipnode, mid) == CONTENTS_SOLID)
	while (1)
	{
	//	shouldn't really happen, but does occasionally
		contents = SV_HullPointContents (hull, hull->firstclipnode, mid);
	//	if (contents != CONTENTS_SOLID && (contents == CONTENTS_WATER || move_type != MOVE_WATER))
		if (contents != CONTENTS_SOLID)
			break;

		frac -= 0.1;
		if (frac < 0)
		{
			trace->fraction = midf;
			VectorCopy (mid, trace->endpos);
			Con_DPrintf ("backup past 0\n");
			return false;
		}
		midf = p1f + (p2f - p1f)*frac;

	//	for (i = 0; i < 3; i++)
	//		mid[i] = p1[i] + frac * (p2[i] - p1[i]);

		WackyBugFixer(p1, p2, &frac, mid);
	}

	trace->fraction = midf;
	VectorCopy (mid, trace->endpos);

	return false;
}


/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
static trace_t SV_ClipMoveToEntity (edict_t *ent, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t *move_ent)
{
	trace_t		trace;
	vec3_t		offset;
	vec3_t		start_l, end_l;
	hull_t		*hull;

// fill in a default trace
	memset (&trace, 0, sizeof(trace_t));
	trace.fraction = 1;
	trace.allsolid = true;
	VectorCopy (end, trace.endpos);

// get the clipping hull
	hull = SV_HullForEntity (ent, mins, maxs, offset, move_ent);

	VectorSubtract (start, offset, start_l);
	VectorSubtract (end, offset, end_l);

	// rotate start and end into the models frame of reference
	if (ent->v.solid == SOLID_BSP && 
		(fabs(ent->v.angles[0]) > 1 || fabs(ent->v.angles[1]) > 1 || fabs(ent->v.angles[2]) > 1) )
	{
		vec3_t	forward, right, up;
		vec3_t	temp;

		AngleVectors (ent->v.angles, forward, right, up);

		VectorCopy (start_l, temp);
		start_l[0] = DotProduct (temp, forward);
		start_l[1] = -DotProduct (temp, right);
		start_l[2] = DotProduct (temp, up);

		VectorCopy (end_l, temp);
		end_l[0] = DotProduct (temp, forward);
		end_l[1] = -DotProduct (temp, right);
		end_l[2] = DotProduct (temp, up);
	}

// trace a line through the apropriate clipping hull
	SV_RecursiveHullCheck (hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

	if (move_type == MOVE_WATER)
	{
		if (SV_PointContents (trace.endpos) != CONTENTS_WATER)
		{
			VectorCopy(start_l, trace.endpos);
			trace.fraction = 0;
		}
	}

	// rotate endpos back to world frame of reference
	if (ent->v.solid == SOLID_BSP && 
		(fabs(ent->v.angles[0]) > 1 || fabs(ent->v.angles[1]) > 1 || fabs(ent->v.angles[2]) > 1) )
	{
		vec3_t	a;
		vec3_t	forward, right, up;
		vec3_t	temp;

		if (trace.fraction != 1)
		{
			VectorNegate (ent->v.angles, a);
			AngleVectors (a, forward, right, up);

			VectorCopy (trace.endpos, temp);
			trace.endpos[0] = DotProduct (temp, forward);
			trace.endpos[1] = -DotProduct (temp, right);
			trace.endpos[2] = DotProduct (temp, up);

			VectorCopy (trace.plane.normal, temp);
			trace.plane.normal[0] = DotProduct (temp, forward);
			trace.plane.normal[1] = -DotProduct (temp, right);
			trace.plane.normal[2] = DotProduct (temp, up);
		}
	}

// fix trace up by the offset
	if (trace.fraction != 1)
		VectorAdd (trace.endpos, offset, trace.endpos);

// did we clip the move?
	if (trace.fraction < 1 || trace.startsolid)
		trace.ent = ent;

	return trace;
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
static void SV_ClipToLinks (areanode_t *node, moveclip_t *clip)
{
	link_t		*l, *next;
	edict_t		*touch;
	trace_t		trace;

loc0: // optimized recursion

// touch linked edicts
	for (l = node->solid_edicts.next ; l != &node->solid_edicts ; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA(l);
		if (touch->v.solid == SOLID_NOT)
			continue;
		if (touch == clip->passedict)
			continue;
		if (touch->v.solid == SOLID_TRIGGER)
			Sys_Error ("Trigger in clipping list (%s)", PR_GetString(touch->v.classname));

		if ((clip->type == MOVE_NOMONSTERS || clip->type == MOVE_PHASE)
				&& touch->v.solid != SOLID_BSP)
			continue;

		if (clip->boxmins[0] > touch->v.absmax[0]
				|| clip->boxmins[1] > touch->v.absmax[1]
				|| clip->boxmins[2] > touch->v.absmax[2]
				|| clip->boxmaxs[0] < touch->v.absmin[0]
				|| clip->boxmaxs[1] < touch->v.absmin[1]
				|| clip->boxmaxs[2] < touch->v.absmin[2] )
			continue;

		if (clip->passedict && clip->passedict->v.size[0] && !touch->v.size[0])
			continue;	// points never interact

	// might intersect, so do an exact clip
		if (clip->trace.allsolid)
			return;
		if (clip->passedict)
		{
			if (PROG_TO_EDICT(touch->v.owner) == clip->passedict)
				continue;	// don't clip against own missiles
			if (PROG_TO_EDICT(clip->passedict->v.owner) == touch)
				continue;	// don't clip against owner
		}

		if ((int)touch->v.flags & FL_MONSTER)
			trace = SV_ClipMoveToEntity (touch, clip->start, clip->mins2, clip->maxs2, clip->end, touch);
		else
			trace = SV_ClipMoveToEntity (touch, clip->start, clip->mins, clip->maxs, clip->end, touch);
		if (trace.allsolid || trace.startsolid || trace.fraction < clip->trace.fraction)
		{
			trace.ent = touch;
			if (clip->trace.startsolid)
			{
				clip->trace = trace;
				clip->trace.startsolid = true;
			}
			else
				clip->trace = trace;
		}
		else if (trace.startsolid)
			clip->trace.startsolid = true;
	}

// recurse down both sides
	if (node->axis == -1)
		return;

	if ( clip->boxmaxs[node->axis] > node->dist )
	{
		//SV_ClipToLinks ( node->children[0], clip );
		if (clip->boxmins[node->axis] < node->dist)
			SV_ClipToLinks(node->children[1], clip);
		node = node->children[0];
		goto loc0;
	}
	else if ( clip->boxmins[node->axis] < node->dist )
	{
		//SV_ClipToLinks ( node->children[1], clip );
		node = node->children[1];
		goto loc0;
	}
}


/*
==================
SV_MoveBounds
==================
*/
static void SV_MoveBounds (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
#if 0
	// debug to test against everything
	boxmins[0] = boxmins[1] = boxmins[2] = -9999;
	boxmaxs[0] = boxmaxs[1] = boxmaxs[2] = 9999;
#else
	int		i;

	for (i = 0; i < 3; i++)
	{
		if (end[i] > start[i])
		{
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
#endif
}

/*
==================
SV_Move
==================
*/
trace_t SV_Move (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int type, edict_t *passedict)
{
	moveclip_t	clip;
	int			i;

//	type = MOVE_WATER;
	memset ( &clip, 0, sizeof ( moveclip_t ) );

	move_type = type;
// clip to world
	clip.trace = SV_ClipMoveToEntity ( sv.edicts, start, mins, maxs, end, passedict );

	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.type = type;
	clip.passedict = passedict;

	if (type == MOVE_MISSILE || type == MOVE_PHASE)
	{
	//Larger for projectiles against monsters
		for (i = 0; i < 3; i++)
		{
			clip.mins2[i] = -15;
			clip.maxs2[i] = 15;
		}
	}
	else
	{
		VectorCopy (mins, clip.mins2);
		VectorCopy (maxs, clip.maxs2);
	}

// create the bounding box of the entire move
	SV_MoveBounds ( start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs );

// clip to entities
	SV_ClipToLinks ( sv_areanodes, &clip );

	return clip.trace;
}

