// SONIC ROBO BLAST 2 KART
//-----------------------------------------------------------------------------
// Copyright (C) 2024 by Kart Krew.
// Copyright (C) 2020 by Sonic Team Junior.
// Copyright (C) 2000 by DooM Legacy Team.
// Copyright (C) 1996 by id Software, Inc.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  hw_portal.c
/// \brief OpenGL renderer portals.

// ==========================================================================
// Visual portals (attr. Hannu Hanhi)
// ==========================================================================

#ifdef HWRENDER

#include "../r_portal.h"

#include "../z_zone.h"

#include "hw_clip.h"
#include "hw_drv.h"
#include "hw_defs.h"
#include "hw_main.h"
#include "hw_portal.h"

SINT8 gl_portal_state = GLPORTAL_OFF;

gl_portallist_t *currentportallist;

// clip the area outside the portal destination window
void HWR_PortalClipping(gl_portal_t *portal)
{
	gld_clipper_SafeAddClipRange(portal->angle1, portal->angle2);
}

static gl_portal_t* HWR_Portal_Add (seg_t *seg)
{
	gl_portal_t *portal = Z_Malloc(sizeof(gl_portal_t), PU_STATIC, NULL);

	// Linked list.
	if (!currentportallist->base)
	{
		currentportallist->base	= portal;
		currentportallist->cap	= portal;
	}
	else
	{
		currentportallist->cap->next = portal;
		currentportallist->cap = portal;
	}
	portal->next = NULL;

	portal->seg = seg;

	return portal;
}

void HWR_FreePortalList(gl_portallist_t freelist)
{
	portalcullsector = NULL;

	// free memory from portal list allocated by calls to Add2Lines
	gl_portal_t *gl_portal_temp = freelist.base;
	while (gl_portal_temp)
	{
		gl_portal_t *nextportal = gl_portal_temp->next;
		Z_Free(gl_portal_temp);
		gl_portal_temp = nextportal;
	}
}

void HWR_Portal_Add2Lines(const INT32 line1, const INT32 line2, seg_t *seg)
{
	gl_portal_t *portal = HWR_Portal_Add(seg);

	// Offset the portal view by the linedef centers
	line_t* start	= &lines[line1];
	line_t* dest	= &lines[line2];

	angle_t dangle = R_PointToAngle2(0, 0, dest->dx, dest->dy) - R_PointToAngle2(start->dx, start->dy, 0, 0);

	fixed_t disttopoint;
	angle_t angtopoint;

	vertex_t dest_c, start_c;

	// looking glass center
	start_c.x = start->v1->x/2 + start->v2->x/2;
	start_c.y = start->v1->y/2 + start->v2->y/2;

	// other side center
	dest_c.x = dest->v1->x/2 + dest->v2->x/2;
	dest_c.y = dest->v1->y/2 + dest->v2->y/2;

	disttopoint = R_PointToDist2(start_c.x, start_c.y, viewx, viewy);
	angtopoint = R_PointToAngle2(start_c.x, start_c.y, viewx, viewy);
	angtopoint += dangle;

	portal->viewx = dest_c.x + FixedMul(FINECOSINE(angtopoint>>ANGLETOFINESHIFT), disttopoint);
	portal->viewy = dest_c.y + FixedMul(FINESINE(angtopoint>>ANGLETOFINESHIFT), disttopoint);
	portal->viewz = viewz + dest->frontsector->floorheight - start->frontsector->floorheight;
	portal->viewangle = viewangle + dangle;

	portal->angle1 = R_PointToAngle64(seg->v1->x, seg->v1->y) + dangle;
	portal->angle2 = R_PointToAngle64(seg->v2->x, seg->v2->y) + dangle;

	portal->clipline = line2;
}

void HWR_PortalFrame(gl_portal_t* portal)
{
	viewx = portal->viewx;
	viewy = portal->viewy;
	viewz = portal->viewz;

	viewangle = portal->viewangle;
	//viewsin = FINESINE(viewangle>>ANGLETOFINESHIFT);
	//viewcos = FINECOSINE(viewangle>>ANGLETOFINESHIFT);

	if (portal->clipline != -1)
	{
		portalclipline = &lines[portal->clipline];
		portalcullsector = portalclipline->frontsector;
		viewsector = portalclipline->frontsector;
	}
	else
	{
		portalclipline = NULL;
		portalcullsector = NULL;
		viewsector = R_PointInSubsector(viewx, viewy)->sector;
	}
}

// Renders a portal segment.
static void HWR_RenderPortalSeg(gl_portal_t* portal, SINT8 state)
{
	gl_drawing_stencil = true; // do not draw outside of the stencil buffer, idiot.
	// set our portal state and prepare to render the seg
	HWR_SetPortalState(state);
	gl_curline = portal->seg;
	gl_frontsector = portal->seg->frontsector;
	gl_backsector = portal->seg->backsector;
	HWR_ProcessSeg();
	gl_drawing_stencil = false;
	// need to work around the r_opengl PF_Invisible bug with this call
	// similarly as in the linkdraw hack in HWR_DrawSprites
	HWD.pfnSetBlend(PF_Translucent|PF_Occlude|PF_Masked);
}

// Renders a single portal from the current viewpoint.
void HWR_RenderPortal(gl_portal_t* portal, gl_portal_t* rootportal, const float fpov, player_t *player, int stencil_level)
{
	// draw portal seg to stencil buffer with increment
	HWR_SetTransform(fpov, player);
	HWR_ClearClipper();

	HWR_SetStencilState(HWR_STENCIL_BEGIN, stencil_level);
	HWR_RenderPortalSeg(portal, GLPORTAL_STENCIL);

	// go to portal frame lmao
	HWR_PortalFrame(portal);
	// call HWR_RenderViewpoint
	HWR_RenderViewpoint(portal, fpov, player, stencil_level + 1, true);
	// return to current frame
	if (rootportal)
		HWR_PortalFrame(rootportal);
	else // current frame is not a portal frame but the main view!
	{
		R_SetupFrame(player, false);
		portalclipline = NULL;
	}

	// remove portal seg from stencil buffer
	HWR_SetTransform(fpov, player);
	HWR_ClearClipper();

	HWR_SetStencilState(HWR_STENCIL_REVERSE, stencil_level);
	HWR_RenderPortalSeg(portal, GLPORTAL_STENCIL);

	// draw portal seg to depth buffer
	HWR_ClearClipper();
	HWR_SetStencilState(HWR_STENCIL_DEPTH, stencil_level);
	HWR_RenderPortalSeg(portal, GLPORTAL_DEPTH);
}

// returns true if the point is on the correct (viewable) side of the
// portal destination line
boolean HWR_PortalCheckPointSide(fixed_t x, fixed_t y)
{
	// we are checking if the point is on the viewable side of the portal exit.
	// being exactly on the portal exit line is not enough to pass the test.
	// P_PointOnLineSide could behave differently from this expectation on this case,
	// so first check if the point is precisely on the line, and then if not, check the side.

	vertex_t closest_point;
	P_ClosestPointOnLine(x, y, portalclipline, &closest_point);
	if (closest_point.x != x || closest_point.y != y)
	{
		if (P_PointOnLineSide(x, y, portalclipline) != 1)
			return true;
	}
	return false;
}

boolean HWR_PortalCheckBBox(const fixed_t *bspcoord)
{
	if (!portalclipline)
		return true;

	if (HWR_PortalCheckPointSide(bspcoord[BOXLEFT], bspcoord[BOXTOP]) ||
		HWR_PortalCheckPointSide(bspcoord[BOXLEFT], bspcoord[BOXBOTTOM]) ||
		HWR_PortalCheckPointSide(bspcoord[BOXRIGHT], bspcoord[BOXTOP]) ||
		HWR_PortalCheckPointSide(bspcoord[BOXRIGHT], bspcoord[BOXBOTTOM]))
	{
		return true;
	}

	// we did not find any reason to pass the check, so return failure
	return false;
}

#endif
