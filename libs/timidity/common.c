/*
 * TiMidity -- Experimental MIDI to WAVE converter
 * Copyright (C) 1995 Tuukka Toivonen <toivonen@clinet.fi>
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
 *
 * common.c
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* I guess "rb" should be right for any libc */
#define OPEN_MODE "rb"

#include "timidity.h"
#include "timidity_internal.h"
#include "common.h"
#include "filenames.h"

/* The paths in this list will be tried whenever open_file()
   reads a file */
struct _PathList {
  char *path;
  struct _PathList *next;
};

static PathList *pathlist = NULL;

/* This is meant to find and open files for reading */
FILE *open_file(const char *name)
{
  FILE *fp;

  if (!name || !(*name))
    {
      DEBUG_MSG("Attempted to open nameless file.\n");
      return NULL;
    }

  /* First try the given name */
  DEBUG_MSG("Trying to open %s\n", name);
  if ((fp = fopen(name, OPEN_MODE)))
    return fp;

  if (!IS_ABSOLUTE_PATH(name))
  {
    char current_filename[TIM_MAXPATH];
    PathList *plp = pathlist;
    int l;

    while (plp)  /* Try along the path then */
      {
	*current_filename = 0;
	l = strlen(plp->path);
	if(l)
	  {
	    strcpy(current_filename, plp->path);
	    if (!IS_DIR_SEPARATOR(current_filename[l - 1]))
	    {
	      current_filename[l] = DIR_SEPARATOR_CHAR;
	      current_filename[l + 1] = '\0';
	    }
	  }
	strcat(current_filename, name);
	DEBUG_MSG("Trying to open %s\n", current_filename);
	if ((fp = fopen(current_filename, OPEN_MODE)))
	  return fp;
	plp = plp->next;
      }
  }

  /* Nothing could be opened. */
  DEBUG_MSG("Could not open %s\n", name);
  return NULL;
}

/* This'll allocate memory and clear it. */
jmp_buf malloc_env;
void *safe_malloc(size_t count)
{
  void *p = malloc(count);
  if (p == NULL) {
    DEBUG_MSG("malloc() failed for %lu bytes.\n", (unsigned long)count);
    longjmp(malloc_env, 1);
  }
  memset(p, 0, count);
  return p;
}

/* This adds a directory to the path list */
void add_to_pathlist(const char *s, size_t l)
{
  PathList *plp = (PathList *) safe_malloc(sizeof(PathList));
  plp->next = pathlist;
  pathlist = plp;
  plp->path = (char *) safe_malloc(l + 1);
  strncpy(plp->path, s, l);
}

void free_pathlist(void)
{
    PathList *plp = pathlist;
    PathList *next;

    while (plp)
    {
	next = plp->next;
	safe_free(plp->path);
	free(plp);
	plp = next;
    }
    pathlist = NULL;
}
