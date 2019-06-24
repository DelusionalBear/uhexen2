/*
 * common.c -- misc utility functions used in client and server
 * $Id$
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 2008-2012  O.Sezer <sezero@users.sourceforge.net>
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
#include "q_ctype.h"
#include "filenames.h"

int		safemode;
extern searchpath_t *fs_searchpaths;
extern searchpath_t *fs_base_searchpaths;


/*
============================================================================

REPLACEMENT FUNCTIONS

============================================================================
*/

int q_strcasecmp(const char * s1, const char * s2)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

	if (p1 == p2)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0')
			break;
	} while (c1 == c2);

	return (int)(c1 - c2);
}

int q_strncasecmp(const char *s1, const char *s2, size_t n)
{
	const char * p1 = s1;
	const char * p2 = s2;
	char c1, c2;

	if (p1 == p2 || n == 0)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0' || c1 != c2)
			break;
	} while (--n > 0);

	return (int)(c1 - c2);
}

char *q_strlwr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_tolower(*c);
		c++;
	}
	return str;
}

char *q_strupr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = q_toupper(*c);
		c++;
	}
	return str;
}

#ifdef __DJGPP__ /* override stock DJGPP versions of str[n]icmp by our q_str[n]casecmp: */
#ifdef __cplusplus
extern "C" {
#endif
int __stricmp(const char *, const char *) __attribute__((alias("q_strcasecmp")));
int stricmp(const char *, const char *) __attribute__((alias("q_strcasecmp")));
int strcasecmp(const char *, const char *) __attribute__((alias("q_strcasecmp")));
int __strnicmp(const char *, const char *, size_t) __attribute__((alias("q_strncasecmp")));
int strnicmp(const char *, const char *, size_t) __attribute__((alias("q_strncasecmp")));
int strncasecmp(const char *, const char *, size_t) __attribute__((alias("q_strncasecmp")));
char *strlwr(char *) __attribute__((alias("q_strlwr")));
char *strupr(char *) __attribute__((alias("q_strlwr")));
#ifdef __cplusplus
}
#endif
#endif

size_t qerr_strlcat (const char *caller, int linenum,
		     char *dst, const char *src, size_t size)
{
	size_t	ret = q_strlcat (dst, src, size);
	if (ret >= size)
		Sys_Error("%s: %d: string buffer overflow!", caller, linenum);
	return ret;
}

size_t qerr_strlcpy (const char *caller, int linenum,
		     char *dst, const char *src, size_t size)
{
	size_t	ret = q_strlcpy (dst, src, size);
	if (ret >= size)
		Sys_Error("%s: %d: string buffer overflow!", caller, linenum);
	return ret;
}

int qerr_snprintf (const char *caller, int linenum,
		   char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list		argptr;

	va_start (argptr, format);
	ret = q_vsnprintf (str, size, format, argptr);
	va_end (argptr);

	if ((size_t)ret >= size)
		Sys_Error("%s: %d: string buffer overflow!", caller, linenum);
	return ret;
}


/*
============================================================================

MISC UTILITY FUNCTIONS

============================================================================
*/

/*
============
va

does a varargs printf into a temp buffer. cycles between
4 different static buffers. the number of buffers cycled
is defined in VA_NUM_BUFFS.
============
*/
#define	VA_NUM_BUFFS	4
#define	VA_BUFFERLEN	1024

static char *get_va_buffer(void)
{
	static char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static int buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char *va (const char *format, ...)
{
	va_list		argptr;
	char		*va_buf;

	va_buf = get_va_buffer ();
	va_start (argptr, format);
	if (q_vsnprintf(va_buf, VA_BUFFERLEN, format, argptr) >= VA_BUFFERLEN)
		Con_DPrintf("%s: overflow (string truncated)\n", __thisfunc__);
	va_end (argptr);

	return va_buf;
}


/*============================================================================
  quick'n'dirty string comparison function for use with qsort
  ============================================================================*/

int COM_StrCompare (const void *arg1, const void *arg2)
{
	return q_strcasecmp ( *(char **) arg1, *(char **) arg2);
}


/*============================================================================
  FileName Processing utilities
  ============================================================================*/

/*
============
COM_SkipPath
============
*/
const char *COM_SkipPath (const char *pathname)
{
	const char	*last;

	last = pathname;
	while (*pathname)
	{
		if (IS_DIR_SEPARATOR(*pathname))
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (const char *in, char *out, size_t outsize)
{
	int	length;

	if (!*in)
	{
		*out = '\0';
		return;
	}
	if (in != out)	/* copy when not in-place editing */
		q_strlcpy (out, in, outsize);
	length = (int)strlen(out) - 1;
	while (length > 0 && out[length] != '.')
	{
		--length;
		if (IS_DIR_SEPARATOR(out[length]))
			return;	/* no extension */
	}
	if (length > 0)
		out[length] = '\0';
}

/*
============
COM_FileGetExtension - doesn't return NULL
============
*/
const char *COM_FileGetExtension (const char *in)
{
	const char	*src;
	size_t		len;

	len = strlen(in);
	if (len < 2)	/* nothing meaningful */
		return "";

	src = in + len - 1;
	while (src != in && src[-1] != '.')
		src--;
	if (src == in || FIND_FIRST_DIRSEP(src) != NULL || HAS_DRIVE_SPEC(src))
		return "";	/* no extension, or parent directory has a dot */

	return src;
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile(int h)
{
	searchpath_t	*s;

	for (s = fs_searchpaths; s; s = s->next)
		if (s->pack && s->pack->handle == h)
			return;

	Sys_FileClose(h);
}

/*
============
COM_ExtractExtension
============
*/
void COM_ExtractExtension (const char *in, char *out, size_t outsize)
{
	const char *ext = COM_FileGetExtension (in);
	if (! *ext)
		*out = '\0';
	else
		q_strlcpy (out, ext, outsize);
}

/*
============
COM_FileBase
take 'somedir/otherdir/filename.ext',
write only 'filename' to the output
============
*/
void COM_FileBase (const char *in, char *out, size_t outsize)
{
	COM_StripExtension (COM_SkipPath(in), out, outsize);
}

/*
==================
COM_DefaultExtension
if path doesn't have a .EXT, append extension
(extension should include the leading ".")
==================
*/
#if 0 /* can be dangerous */
void COM_DefaultExtension (char *path, const char *extension, size_t len)
{
	char	*src;

	if (!*path) return;
	src = path + strlen(path) - 1;

	while (!IS_DIR_SEPARATOR(*src) && src != path)
	{
		if (*src == '.')
			return; // it has an extension
		src--;
	}

	qerr_strlcat(__thisfunc__, __LINE__, path, extension, len);
}
#endif

/*
==================
COM_AddExtension
if path extension doesn't match .EXT, append it
(extension should include the leading ".")
==================
*/
void COM_AddExtension (char *path, const char *extension, size_t len)
{
	if (strcmp(COM_FileGetExtension(path), extension + 1) != 0)
		qerr_strlcat(__thisfunc__, __LINE__, path, extension, len);
}

/*
================
COM_filelength
================
*/
long COM_filelength(FILE *f)
{
	long		pos, end;

	pos = ftell(f);
	fseek(f, 0, SEEK_END);
	end = ftell(f);
	fseek(f, pos, SEEK_SET);

	return end;
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
#define	LOADFILE_ZONE		0
#define	LOADFILE_HUNK		1
#define	LOADFILE_TEMPHUNK	2
#define	LOADFILE_CACHE		3
#define	LOADFILE_STACK		4
#define	LOADFILE_MALLOC		5

static byte	*loadbuf;
static cache_user_t *loadcache;
static int	loadsize;

byte *COM_LoadFile(const char *path, int usehunk, unsigned int *path_id)
{
	int		h;
	byte	*buf;
	char	base[32];
	int		len;

	buf = NULL;	// quiet compiler warning

// look for it in the filesystem or pack files
	len = FS_OpenFile(path, &h, path_id);
	if (h == -1)
		return NULL;

	// extract the filename base name for hunk tag
	COM_FileBase(path, base, sizeof(base));

	switch (usehunk)
	{
	case LOADFILE_HUNK:
		buf = (byte *)Hunk_AllocName(len + 1, base);
		break;
	case LOADFILE_TEMPHUNK:
		buf = (byte *)Hunk_TempAlloc(len + 1);
		break;
	case LOADFILE_ZONE:
		buf = (byte *)Z_Malloc(len + 1, Z_MAINZONE);
		break;
	case LOADFILE_CACHE:
		buf = (byte *)Cache_Alloc(loadcache, len + 1, base);
		break;
	case LOADFILE_STACK:
		if (len < loadsize)
			buf = loadbuf;
		else
			buf = (byte *)Hunk_TempAlloc(len + 1);
		break;
	case LOADFILE_MALLOC:
		buf = (byte *)malloc(len + 1);
		break;
	default:
		Sys_Error("COM_LoadFile: bad usehunk");
	}

	if (!buf)
		Sys_Error("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	Sys_FileRead(h, buf, len);
	COM_CloseFile(h);

	return buf;
}


/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
If neither of file or handle is set, this
can be used for detecting a file's presence.
===========
*/
static int COM_FindFile(const char *filename, int *handle, FILE **file,
	unsigned int *path_id)
{
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pak;
	int		i, findtime;

	if (file && handle)
		Sys_Error("COM_FindFile: both handle and file set");

	file_from_pak = 0;

	//
	// search through the path, one element at a time
	//
	for (search = fs_searchpaths; search; search = search->next)
	{
		if (search->pack)	/* look through all the pak file elements */
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp(pak->files[i].name, filename) != 0)
					continue;
				// found it!
				fs_filesize = pak->files[i].filelen;
				file_from_pak = 1;
				if (path_id)
					*path_id = search->path_id;
				if (handle)
				{
					*handle = pak->handle;
					Sys_FileSeek(pak->handle, pak->files[i].filepos);
					return fs_filesize;
				}
				else if (file)
				{ /* open a new file on the pakfile */
					*file = fopen(pak->filename, "rb");
					if (*file)
						fseek(*file, pak->files[i].filepos, SEEK_SET);
					return fs_filesize;
				}
				else /* for COM_FileExists() */
				{
					return fs_filesize;
				}
			}
		}
		else	/* check a file in the directory tree */
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if (strchr(filename, '/') || strchr(filename, '\\'))
					continue;
			}

			q_snprintf(netpath, sizeof(netpath), "%s/%s", search->filename, filename);
			findtime = Sys_FileTime(netpath);
			if (findtime == -1)
				continue;

			if (path_id)
				*path_id = search->path_id;
			if (handle)
			{
				fs_filesize = Sys_FileOpenRead(netpath, &i);
				*handle = i;
				return fs_filesize;
			}
			else if (file)
			{
				*file = fopen(netpath, "rb");
				fs_filesize = (*file == NULL) ? -1 : COM_filelength(*file);
				return fs_filesize;
			}
			else
			{
				return 0; /* dummy valid value for COM_FileExists() */
			}
		}
	}

	if (strcmp(COM_FileGetExtension(filename), "pcx") != 0
		&& strcmp(COM_FileGetExtension(filename), "tga") != 0
		&& strcmp(COM_FileGetExtension(filename), "lit") != 0
		&& strcmp(COM_FileGetExtension(filename), "ent") != 0)
		Con_DPrintf("FindFile: can't find %s\n", filename);
	else	Con_DPrintf("FindFile: can't find %s\n", filename);
	// Log pcx, tga, lit, ent misses only if (developer.value >= 2)

	if (handle)
		*handle = -1;
	if (file)
		*file = NULL;
	fs_filesize = -1;
	return fs_filesize;
}

/*
===========
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
int COM_FOpenFile(const char *filename, FILE **file, unsigned int *path_id)
{
	return COM_FindFile(filename, NULL, file, path_id);
}

/*
============================================================================

STRING PARSING FUNCTIONS

============================================================================
*/

char		com_token[1024];

/*
==============
COM_Parse

Parse a token out of a string
==============
*/
const char *COM_Parse (const char *data)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;	// end of file
		data++;
	}

// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		data += 2;
		while (*data && !(*data == '*' && data[1] == '/'))
			data++;
		if (*data)
			data += 2;
		goto skipwhite;
	}

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if ((c = *data) != 0)
				++data;
			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

#if 0
// parse single characters
	if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data+1;
	}
#endif

// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;
#if 0
		if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
			break;
#endif
	} while (c > 32);

	com_token[len] = 0;
	return data;
}


/*
============================================================================

COMMAND LINE PROCESSING FUNCTIONS

============================================================================
*/

/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParm (const char *parm)
{
	int		i;

	for (i = 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue;		// NEXTSTEP sometimes clears appkit vars.
		if (!strcmp (parm,com_argv[i]))
			return i;
	}

	return 0;
}

static void COM_Cmdline_f (void)
{
	int			i;

	Con_Printf ("cmdline was:");
	for (i = 0; (i < MAX_NUM_ARGVS) && (i < com_argc); i++)
	{
		if (com_argv[i])
			Con_Printf (" %s", com_argv[i]);
	}
	Con_Printf ("\n");
}

/*
============================================================================

INIT, ETC..

============================================================================
*/

void COM_ValidateByteorder (void)
{
	const char	*endianism[] = { "BE", "LE", "PDP", "Unknown" };
	const char	*tmp;

	ByteOrder_Init ();
	switch (host_byteorder)
	{
	case BIG_ENDIAN:
		tmp = endianism[0];
		break;
	case LITTLE_ENDIAN:
		tmp = endianism[1];
		break;
	case PDP_ENDIAN:
		tmp = endianism[2];
		host_byteorder = -1;	/* not supported */
		break;
	default:
		tmp = endianism[3];
		break;
	}
	if (host_byteorder < 0)
		Sys_Error ("%s: Unsupported byte order [%s]", __thisfunc__, tmp);
	Sys_Printf("Detected byte order: %s\n", tmp);
#if !ENDIAN_RUNTIME_DETECT
	if (host_byteorder != BYTE_ORDER)
	{
		const char	*tmp2;

		switch (BYTE_ORDER)
		{
		case BIG_ENDIAN:
			tmp2 = endianism[0];
			break;
		case LITTLE_ENDIAN:
			tmp2 = endianism[1];
			break;
		case PDP_ENDIAN:
			tmp2 = endianism[2];
			break;
		default:
			tmp2 = endianism[3];
			break;
		}
		Sys_Error ("Detected byte order %s doesn't match compiled %s order!", tmp, tmp2);
	}
#endif	/* ENDIAN_RUNTIME_DETECT */
}

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	Cmd_AddCommand ("cmdline", COM_Cmdline_f);

	safemode = COM_CheckParm ("-safe");
}

