/*
	common.c
	misc functions used in client and server

	$Id: common.c,v 1.79 2007-02-02 14:17:46 sezero Exp $
*/

#if defined(H2W) && defined(SERVERONLY)
#include "qwsvdef.h"
#else
#include "quakedef.h"
#endif
#ifdef _WIN32
#include <io.h>
#endif
#ifdef PLATFORM_UNIX
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#endif
#include <ctype.h>


#define NUM_SAFE_ARGVS	6

static char	*largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1];
static char	*argvdummy = " ";

static char	*safeargvs[NUM_SAFE_ARGVS] =
	{"-nomidi", "-nolan", "-nosound", "-nocdaudio", "-nojoy", "-nomouse"};

cvar_t	registered = {"registered", "0", CVAR_ROM};
cvar_t	oem = {"oem", "0", CVAR_ROM};

unsigned int	gameflags;

qboolean		msg_suppress_1 = 0;

static void COM_InitFilesystem (void);
static void COM_Path_f (void);
#ifndef SERVERONLY
static void COM_Maplist_f (void);
#endif

// look-up table of pak filenames: { numfiles, crc }
// if a packfile directory differs from this, it is assumed to be hacked
#define MAX_PAKDATA	6
static const int pakdata[MAX_PAKDATA][2] = {
	{ 696,	34289 },	/* pak0.pak, registered	*/
	{ 523,	2995  },	/* pak1.pak, registered	*/
	{ 183,	4807  },	/* pak2.pak, oem, data needs verification */
	{ 245,	1478  },	/* pak3.pak, portals	*/
	{ 102,	41062 },	/* pak4.pak, hexenworld	*/
	{ 797,	22780 }		/* pak0.pak, demo v1.11	*/
//	{ 701,	20870 }		/* pak0.pak, old 1.07 version of the demo */
//	The old v1.07 demo on the ID Software ftp isn't supported
//	(pak0.pak::progs.dat : 19267 crc, progheader crc : 14046)
};

// loacations of pak filenames as shipped by raven
static const char *dirdata[MAX_PAKDATA] = {
	"data1",	/* pak0.pak, registered	*/
	"data1",	/* pak1.pak, registered	*/
	"data1",	/* pak2.pak, oem	*/
	"portals",	/* pak3.pak, portals	*/
	"hw",		/* pak4.pak, hexenworld	*/
	"data1"		/* pak0.pak, demo	*/
};

char	gamedirfile[MAX_OSPATH];

#define CMDLINE_LENGTH	256
static char	com_cmdline[CMDLINE_LENGTH];

// this graphic needs to be in the pak file to use registered features
static const unsigned short pop[] =
{
 0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000
,0x0000,0x0000,0x6600,0x0000,0x0000,0x0000,0x6600,0x0000
,0x0000,0x0066,0x0000,0x0000,0x0000,0x0000,0x0067,0x0000
,0x0000,0x6665,0x0000,0x0000,0x0000,0x0000,0x0065,0x6600
,0x0063,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6563
,0x0064,0x6561,0x0000,0x0000,0x0000,0x0000,0x0061,0x6564
,0x0064,0x6564,0x0000,0x6469,0x6969,0x6400,0x0064,0x6564
,0x0063,0x6568,0x6200,0x0064,0x6864,0x0000,0x6268,0x6563
,0x0000,0x6567,0x6963,0x0064,0x6764,0x0063,0x6967,0x6500
,0x0000,0x6266,0x6769,0x6a68,0x6768,0x6a69,0x6766,0x6200
,0x0000,0x0062,0x6566,0x6666,0x6666,0x6666,0x6562,0x0000
,0x0000,0x0000,0x0062,0x6364,0x6664,0x6362,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0062,0x6662,0x0000,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0061,0x6661,0x0000,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0000,0x6500,0x0000,0x0000,0x0000
,0x0000,0x0000,0x0000,0x0000,0x6400,0x0000,0x0000,0x0000
};


//============================================================================

/*
All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the exe and all game
directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.
This can be overridden with the "-basedir" command line parm to allow code
debugging in a different directory.  The base directory is only used during
filesystem initialization.

The "game directory" is the first tree on the search path and directory that
all generated files (savegames, screenshots, demos, config files) will be saved
to.  This can be overridden with the "-game" command line parameter.  The game
directory can never be changed while quake is executing.  This is a precacution
against having a malicious server instruct clients to write files over areas
they shouldn't.

The "cache directory" is only used during development to save network bandwidth
especially over ISDN / T1 lines.  If there is a cache directory specified, when
a file is found by the normal search path, it will be mirrored into the cache
directory, then opened there.
*/


//============================================================================

// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

short ShortSwap (short l)
{
	byte	b1, b2;

	b1 = l&255;
	b2 = (l>>8)&255;

	return (b1<<8) + b2;
}

int LongSwap (int l)
{
	byte	b1, b2, b3, b4;

	b1 = l&255;
	b2 = (l>>8)&255;
	b3 = (l>>16)&255;
	b4 = (l>>24)&255;

	return ((int)b1<<24) + ((int)b2<<16) + ((int)b3<<8) + b4;
}

float FloatSwap (float f)
{
	union
	{
		float	f;
		byte	b[4];
	} dat1, dat2;

	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}


/*
============================================================================

MISC REPLACEMENT FUNCTIONS

============================================================================
*/

char *Q_strlwr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = tolower(*c);
		c++;
	}
	return str;
}

char *Q_strupr (char *str)
{
	char	*c;
	c = str;
	while (*c)
	{
		*c = toupper(*c);
		c++;
	}
	return str;
}


/*
==============================================================================

Q_MALLOC / Q_FREE

malloc and free system memory. LordHavoc.
==============================================================================
*/

static unsigned int	qmalloctotal_alloc,
			qmalloctotal_alloccount,
			qmalloctotal_free,
			qmalloctotal_freecount;

void *Q_malloc(unsigned int size)
{
	unsigned int	*mem;

	qmalloctotal_alloc += size;
	qmalloctotal_alloccount++;
	mem = malloc(size+sizeof(unsigned int));
	if (!mem)
		return mem;
	*mem = size;

	return (void *)(mem + 1);
}

void Q_free(void *mem)
{
	unsigned int	*m;

	if (!mem)
		return;
	m = mem;
	m--;	// back up to size
	qmalloctotal_free += *m;	// size
	qmalloctotal_freecount++;
	free(m);
}


/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < -128 || c > 127)
		Sys_Error ("MSG_WriteChar: range error");
#endif

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < 0 || c > 255)
		Sys_Error ("MSG_WriteByte: range error");
#endif

	buf = SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte	*buf;

#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Sys_Error ("MSG_WriteShort: range error");
#endif

	buf = SZ_GetSpace (sb, 2);
	buf[0] = c&0xff;
	buf[1] = c>>8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte	*buf;

	buf = SZ_GetSpace (sb, 4);
	buf[0] = c&0xff;
	buf[1] = (c>>8)&0xff;
	buf[2] = (c>>16)&0xff;
	buf[3] = c>>24;
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float	f;
		int	l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteString (sizebuf_t *sb, char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, strlen(s)+1);
}

void MSG_WriteCoord (sizebuf_t *sb, float f)
{
//	MSG_WriteShort (sb, (int)(f*8));
	if (f >= 0)
		MSG_WriteShort (sb, (int)(f * 8.0 + 0.5));
	else
		MSG_WriteShort (sb, (int)(f * 8.0 - 0.5));
}

void MSG_WriteAngle (sizebuf_t *sb, float f)
{
//	MSG_WriteByte (sb, (int)(f*256/360) & 255);
//	LordHavoc: round to nearest value, rather than rounding toward zero
	if (f >= 0)
		MSG_WriteByte (sb, (int)(f*(256.0/360.0) + 0.5) & 255);
	else
		MSG_WriteByte (sb, (int)(f*(256.0/360.0) - 0.5) & 255);
}

#ifdef H2W
void MSG_WriteAngle16 (sizebuf_t *sb, float f)
{
//	MSG_WriteShort (sb, (int)(f*65536/360) & 65535);
	if (f >= 0)
		MSG_WriteShort (sb, (int)(f*(65536.0/360.0) + 0.5) & 65535);
	else
		MSG_WriteShort (sb, (int)(f*(65536.0/360.0) - 0.5) & 65535);
}

void MSG_WriteUsercmd (sizebuf_t *buf, usercmd_t *cmd, qboolean long_msg)
{
	int		bits;

//
// send the movement message
//
	bits = 0;
	if (cmd->angles[0])
		bits |= CM_ANGLE1;
	if (cmd->angles[2])
		bits |= CM_ANGLE3;
	if (cmd->forwardmove)
		bits |= CM_FORWARD;
	if (cmd->sidemove)
		bits |= CM_SIDE;
	if (cmd->upmove)
		bits |= CM_UP;
	if (cmd->buttons)
		bits |= CM_BUTTONS;
	if (cmd->impulse)
		bits |= CM_IMPULSE;
	if (cmd->msec)
		bits |= CM_MSEC;

	MSG_WriteByte (buf, bits);
	if (long_msg)
	{
		MSG_WriteByte (buf, cmd->light_level);
	}

	if (bits & CM_ANGLE1)
		MSG_WriteAngle16 (buf, cmd->angles[0]);
	MSG_WriteAngle16 (buf, cmd->angles[1]);
	if (bits & CM_ANGLE3)
		MSG_WriteAngle16 (buf, cmd->angles[2]);

	if (bits & CM_FORWARD)
		MSG_WriteChar (buf, (int)(cmd->forwardmove*0.25));
	if (bits & CM_SIDE)
	  	MSG_WriteChar (buf, (int)(cmd->sidemove*0.25));
	if (bits & CM_UP)
		MSG_WriteChar (buf, (int)(cmd->upmove*0.25));

	if (bits & CM_BUTTONS)
	  	MSG_WriteByte (buf, cmd->buttons);
	if (bits & CM_IMPULSE)
		MSG_WriteByte (buf, cmd->impulse);
	if (bits & CM_MSEC)
		MSG_WriteByte (buf, cmd->msec);
}
#endif	// H2W


//
// reading functions
//
int		msg_readcount;
qboolean	msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int	c;

	if (msg_readcount+1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int	c;

	if (msg_readcount+2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	int	c;

	if (msg_readcount+4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = net_message.data[msg_readcount]
			+ (net_message.data[msg_readcount+1]<<8)
			+ (net_message.data[msg_readcount+2]<<16)
			+ (net_message.data[msg_readcount+3]<<24);

	msg_readcount += 4;

	return c;
}

float MSG_ReadFloat (void)
{
	union
	{
		byte	b[4];
		float	f;
		int	l;
	} dat;

	dat.b[0] =	net_message.data[msg_readcount];
	dat.b[1] =	net_message.data[msg_readcount+1];
	dat.b[2] =	net_message.data[msg_readcount+2];
	dat.b[3] =	net_message.data[msg_readcount+3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}

char *MSG_ReadString (void)
{
	static char	string[2048];
	int		l, c;

	l = 0;
	do
	{
		c = MSG_ReadChar ();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string)-1);

	string[l] = 0;

	return string;
}

#ifdef H2W
char *MSG_ReadStringLine (void)
{
	static char	string[2048];
	int		l,c;

	l = 0;
	do
	{
		c = MSG_ReadChar ();
		if (c == -1 || c == 0 || c == '\n')
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string)-1);

	string[l] = 0;

	return string;
}
#endif	// H2W

float MSG_ReadCoord (void)
{
	return MSG_ReadShort() * (1.0/8.0);
}

float MSG_ReadAngle (void)
{
	return MSG_ReadChar() * (360.0/256.0);
}

#ifdef H2W
float MSG_ReadAngle16 (void)
{
	return MSG_ReadShort() * (360.0/65536.0);
}

void MSG_ReadUsercmd (usercmd_t *move, qboolean long_msg)
{
	int bits;

	memset (move, 0, sizeof(*move));

	bits = MSG_ReadByte ();
	if (long_msg)
	{
		move->light_level = MSG_ReadByte();
	}
	else
	{
		move->light_level = 0;
	}

// read current angles
	if (bits & CM_ANGLE1)
		move->angles[0] = MSG_ReadAngle16 ();
	else
		move->angles[0] = 0;
	move->angles[1] = MSG_ReadAngle16 ();
	if (bits & CM_ANGLE3)
		move->angles[2] = MSG_ReadAngle16 ();
	else
		move->angles[2] = 0;

// read movement
	if (bits & CM_FORWARD)
		move->forwardmove = MSG_ReadChar () * 4;
	if (bits & CM_SIDE)
		move->sidemove = MSG_ReadChar () * 4;
	if (bits & CM_UP)
		move->upmove = MSG_ReadChar () * 4;

// read buttons
	if (bits & CM_BUTTONS)
		move->buttons = MSG_ReadByte ();
	else
		move->buttons = 0;

	if (bits & CM_IMPULSE)
		move->impulse = MSG_ReadByte ();
	else
		move->impulse = 0;

// read time to run command
	if (bits & CM_MSEC)
		move->msec = MSG_ReadByte ();
	else
		move->msec = 0;
}
#endif	// H2W


//===========================================================================

void SZ_Init (sizebuf_t *buf, byte *data, int length)
{
	memset (buf, 0, sizeof(*buf));
	buf->data = data;
	buf->maxsize = length;
	//buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
	buf->overflowed = false;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void	*data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		Sys_Printf ("SZ_GetSpace: overflow\nCurrently %d of %d, requested %d\n",buf->cursize,buf->maxsize,length);
		SZ_Clear (buf); 
		buf->overflowed = true;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, void *data, int length)
{
	memcpy (SZ_GetSpace(buf,length),data,length);
}

void SZ_Print (sizebuf_t *buf, char *data)
{
	int		len;

	len = strlen(data)+1;

	if (!buf->cursize || buf->data[buf->cursize-1])
		memcpy ((byte *)SZ_GetSpace(buf, len),data,len); // no trailing 0
	else
		memcpy ((byte *)SZ_GetSpace(buf, len-1)-1,data,len); // write over trailing 0
}


//============================================================================


/*
============
COM_SkipPath
============
*/
char *COM_SkipPath (char *pathname)
{
	char	*last;

	last = pathname;
	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname+1;
		pathname++;
	}
	return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (char *in, char *out)
{
	while (*in && *in != '.')
		*out++ = *in++;
	*out = 0;
}

/*
============
COM_FileExtension
============
*/
char *COM_FileExtension (char *in)
{
	static char exten[8];
	int		i;

	while (*in && *in != '.')
		in++;
	if (!*in)
		return "";
	in++;
	for (i = 0; i < 7 && *in; i++, in++)
		exten[i] = *in;
	exten[i] = 0;
	return exten;
}

/*
============
COM_FileBase
============
*/
void COM_FileBase (char *in, char *out)
{
	char	*s, *s2;

	s = in + strlen(in) - 1;

	while (s != in && *s != '.')
		s--;

	/* Pa3PyX: no range checking -- used to trash the stack and crash the
	   game randomly upon loading progs, for instance (or in any other
	   instance where one would supply a filename without a path	*/
//	for (s2 = s; *s2 && *s2 != '/'; s2--);
	for (s2 = s; *s2 && *s2 != '/' && s2 >= in; s2--)
		;

	if (s-s2 < 2)
		strcpy (out,"?model?");
	else
	{
		s--;
		strncpy (out, s2+1, s-s2);
		out[s-s2] = 0;
	}
}


/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension (char *path, char *extension, size_t len)
{
	char	*src;
//
// if path doesn't have a .EXT, append extension
// (extension should include the .)
//
	src = path + strlen(path) - 1;

	while (*src != '/' && src != path)
	{
		if (*src == '.')
			return;                 // it has an extension
		src--;
	}

	Q_strlcat_err(path, extension, len);
}


// quick'n'dirty string comparison function for use with qsort
int COM_StrCompare (const void *arg1, const void *arg2)
{
	return Q_strcasecmp ( *(char **) arg1, *(char **) arg2);
}

//============================================================================

char		com_token[1024];
int		com_argc;
char	**com_argv;


/*
==============
COM_Parse

Parse a token out of a string
==============
*/
char *COM_Parse (char *data)
{
	int		c;
	int		len;

	len = 0;
	com_token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ( (c = *data) <= ' ')
	{
		if (c == 0)
			return NULL;			// end of file;
		data++;
	}

// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			c = *data++;
			if (c == '\"' || !c)
			{
				com_token[len] = 0;
				return data;
			}
			com_token[len] = c;
			len++;
		}
	}

// parse single characters
/*	if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':')
	{
		com_token[len] = c;
		len++;
		com_token[len] = 0;
		return data+1;
	}
*/
// parse a regular word
	do
	{
		com_token[len] = c;
		data++;
		len++;
		c = *data;

//		if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':')
//			break;
	} while (c > 32);

	com_token[len] = 0;
	return data;
}


/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParm (char *parm)
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

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the registered flag.
================
*/
void COM_CheckRegistered (void)
{
	FILE		*h;
	unsigned short	check[128];
	int			i;

	COM_FOpenFile("gfx/pop.lmp", &h, false);

	if (!h)
		return;

	fread (check, 1, sizeof(check), h);
	fclose (h);

	for (i = 0; i < 128; i++)
	{
		if ( pop[i] != (unsigned short)BigShort(check[i]) )
			Sys_Error ("Corrupted data file.");
	}

	// check if we have 1.11 versions of pak0.pak and pak1.pak
	if (!(gameflags & GAME_REGISTERED0) || !(gameflags & GAME_REGISTERED1))
		Sys_Error ("You must patch your installation with Raven's 1.11 update");

	gameflags |= GAME_REGISTERED;
}


/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	qboolean	safe;
	int		i, j, n;

// reconstitute the command line for the cmdline console command
	n = 0;

	for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
		{
			com_cmdline[n++] = argv[j][i++];
		}

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}

	com_cmdline[n] = 0;

	safe = false;

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!strcmp ("-safe", argv[com_argc]))
			safe = true;
	}

	if (safe)
	{
	// force all the safe-mode switches. Note that we reserved extra space in
	// case we need to add these, so we don't need an overflow check
		for (i = 0; i < NUM_SAFE_ARGVS; i++)
		{
			largv[com_argc] = safeargvs[i];
			com_argc++;
		}
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;
}

#if 0
/*
================
COM_AddParm

Adds the given string at the end of the current argument list
================
*/
void COM_AddParm (char *parm)
{
	largv[com_argc++] = parm;
}
#endif

static void COM_Cmdline_f (void)
{
	Con_Printf ("cmdline is: \"%s\"\n", com_cmdline);
}

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&oem);
	Cmd_AddCommand ("path", COM_Path_f);
	Cmd_AddCommand ("cmdline", COM_Cmdline_f);
#ifndef SERVERONLY
	Cmd_AddCommand ("maplist", COM_Maplist_f);
#endif

	COM_InitFilesystem ();
}


/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
============
*/
#define VA_BUFFERLEN 1024

static char *get_va_buffer(void)
{
	static char va_buffers[4][VA_BUFFERLEN];
	static unsigned char	buf_idx;
	return va_buffers[3 & ++buf_idx];
}

char *va (char *format, ...)
{
	va_list		argptr;
	char		*va_buf;

	va_buf = get_va_buffer ();
	va_start (argptr, format);
	if ( vsnprintf(va_buf, VA_BUFFERLEN, format, argptr) >= VA_BUFFERLEN )
		Con_DPrintf("%s: overflow (string truncated)\n", __FUNCTION__);
	va_end (argptr);

	return va_buf;
}

#if 0
/// just for debugging
int memsearch (byte *start, int count, int search)
{
	int		i;

	for (i = 0; i < count; i++)
	{
		if (start[i] == search)
			return i;
	}
	return -1;
}
#endif

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

size_t		com_filesize;


//
// in memory
//

typedef struct
{
	char	name[MAX_QPATH];
	int		filepos, filelen;
} packfile_t;

typedef struct pack_s
{
	char	filename[MAX_OSPATH];
	FILE	*handle;
	int		numfiles;
	packfile_t	*files;
} pack_t;

//
// on disk
//
typedef struct
{
	char	name[56];
	int		filepos, filelen;
} dpackfile_t;

typedef struct
{
	char	id[4];
	int		dirofs;
	int		dirlen;
} dpackheader_t;

#define	MAX_FILES_IN_PACK	2048

char	com_gamedir[MAX_OSPATH];
char	com_basedir[MAX_OSPATH];
char	com_userdir[MAX_OSPATH];
char	com_savedir[MAX_OSPATH];	// temporary path for saving gip files

typedef struct searchpath_s
{
	char	filename[MAX_OSPATH];
	pack_t	*pack;		// only one of filename / pack will be used
	struct	searchpath_s *next;
} searchpath_t;

static searchpath_t	*com_searchpaths;
static searchpath_t	*com_base_searchpaths;	// without gamedirs

/*
================
COM_filelength
================
*/
static size_t COM_filelength (FILE *f)
{
	long		pos, end;

	pos = ftell (f);
	fseek (f, 0, SEEK_END);
	end = ftell (f);
	fseek (f, pos, SEEK_SET);

	return (size_t)end;
}

/*
============
COM_Path_f

============
*/
static void COM_Path_f (void)
{
	searchpath_t	*s;

	Con_Printf ("Current search path:\n");
	for (s = com_searchpaths ; s ; s = s->next)
	{
		if (s == com_base_searchpaths)
			Con_Printf ("----------\n");
		if (s->pack)
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		else
			Con_Printf ("%s\n", s->filename);
	}
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
Returns 0 on success, 1 on error.
============
*/
int COM_WriteFile (char *filename, void *data, size_t len)
{
	FILE	*f;
	char	name[MAX_OSPATH];
	size_t	size;

	if (snprintf(name, sizeof(name), "%s/%s", com_userdir, filename) >= sizeof(name))
	{
		Con_Printf ("%s: string buffer overflow!\n", __FUNCTION__);
		return 1;
	}

	f = fopen (name, "wb");
	if (!f)
	{
		Con_Printf ("Error opening %s\n", filename);
		return 1;
	}

	Sys_Printf ("%s: %s\n", __FUNCTION__, name);
	size = fwrite (data, 1, len, f);
	fclose (f);
	if (size != len)
	{
		Con_Printf ("Error in writing %s\n", filename);
		return 1;
	}
	return 0;
}


/*
============
COM_CreatePath
Creates directory under user's path,
making parent directories as needed.
The path must either be a path to a
file, or, if the full path is meant to
be created, it must have the trailing
path seperator.
Returns 0 on success, non-zero on error.
Only used for CopyFile and download
============
*/
int COM_CreatePath (char *path)
{
	char	*ofs;
	int		error_state = 0;
	size_t		offset;

	if (!path)
	{
		Con_Printf ("%s: no path!\n", __FUNCTION__);
		return 1;
	}

	if (strstr(path, ".."))
	{
		Con_Printf ("Relative pathnames are not allowed.\n");
		return 1;
	}

	ofs = host_parms.userdir;
	if (strstr(path, ofs) != path)
	{
		Sys_Error ("Attempted to create a directory out of user's path");
		return 1;
	}

	offset = strlen(ofs);
	for (ofs = path+offset ; *ofs ; ofs++)
	{
		if (*ofs == '/')
		{	// create the directory
			*ofs = 0;
			error_state = Sys_mkdir (path);
			*ofs = '/';
			if (error_state)
				break;
		}
	}

	return error_state;
}


/*
===========
COM_CopyFile

Copies a file over from the net to the local cache, creating any directories
needed. Used for saving the game. Returns 0 on success, non-zero on error.
===========
*/
int COM_CopyFile (char *netpath, char *cachepath)
{
	FILE	*in, *out;
	int		err = 0;
	size_t		remaining, count;
	char	buf[4096];

	in = fopen (netpath, "rb");
	if (!in)
	{
		Con_Printf ("%s: unable to open %s\n", netpath, __FUNCTION__);
		return 1;
	}
	remaining = COM_filelength(in);

	// create directories up to the cache file
	if (COM_CreatePath (cachepath))
	{
		Con_Printf ("%s: unable to create directory\n", __FUNCTION__);
		fclose (in);
		return 1;
	}

	out = fopen(cachepath, "wb");
	if (!out)
	{
		Con_Printf ("%s: unable to create %s\n", cachepath, __FUNCTION__);
		fclose (in);
		return 1;
	}

	while (remaining)
	{
		if (remaining < sizeof(buf))
			count = remaining;
		else
			count = sizeof(buf);
		fread (buf, 1, count, in);
		err = ferror(in);
		if (err)
			break;
		fwrite (buf, 1, count, out);
		err = ferror(out);
		if (err)
			break;
		remaining -= count;
	}

	fclose (in);
	fclose (out);
	return err;
}

/*
===========
COM_Maplist_f

Prints map filenames to the console
===========
*/
#ifndef SERVERONLY
static void COM_Maplist_f (void)
{
	int			i, cnt, dups = 0;
	pack_t		*pak;
	searchpath_t	*search;
	char		**maplist = NULL, mappath[MAX_OSPATH];
	char	*findname;

	// do two runs - first count the number of maps
	// then collect their names into maplist
scanmaps:
	cnt = 0;
	// search through the path, one element at a time
	// either "search->filename" or "search->pak" is defined
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->pack)
		{
			pak = search->pack;

			for (i = 0; i < pak->numfiles; i++)
			{
				if (strncmp ("maps/", pak->files[i].name, 5) == 0  && 
				    strstr(pak->files[i].name, ".bsp"))
				{
					if (maplist)
					{
						size_t	len;
						int	dupl = 0, j;
						// add to our maplist
						len = strlen (pak->files[i].name + 5) - 4 + 1;
								// - ".bsp" (-4) +  "\0" (+1)
						for (j = 0 ; j < cnt ; j++)
						{
							if (!Q_strncasecmp(maplist[j], pak->files[i].name + 5, len-1))
							{
								dupl = 1;
								dups++;
								break;
							}
						}
						if (!dupl)
						{
							maplist[cnt] = malloc (len);
							Q_strlcpy ((char *)maplist[cnt] , pak->files[i].name + 5, len);
							cnt++;
						}
					}
					else
						cnt++;
				}
			}
		}
		else
		{	// element is a filename
			snprintf (mappath, sizeof(mappath), search->filename);
			Q_strlcat (mappath, "/maps", sizeof(mappath));
			findname = Sys_FindFirstFile (mappath, "*.bsp");
			while (findname)
			{
				if (maplist)
				{
					size_t	len;
					int	dupl = 0, j;
					// add to our maplist
					len = strlen(findname) - 4 + 1;
					for (j = 0 ; j < cnt ; j++)
					{
						if (!Q_strncasecmp(maplist[j], findname, len-1))
						{
							dupl = 1;
							dups++;
							break;
						}
					}
					if (!dupl)
					{
						maplist[cnt] = malloc (len);
						Q_strlcpy (maplist[cnt], findname, len);
						cnt++;
					}
				}
				else
					cnt++;
				findname = Sys_FindNextFile ();
			}
			Sys_FindClose ();
		}
	}

	if (maplist == NULL)
	{
		// after first run, we know how many maps we have
		// should I use malloc or something else
		Con_Printf ("Found %d maps:\n\n", cnt);
		if (!cnt)
			return;
		maplist = malloc(cnt * sizeof (char *));
		goto scanmaps;
	}

	// sort the list
	qsort (maplist, cnt, sizeof(char *), COM_StrCompare);
	Con_ShowList (cnt, (const char**)maplist);
	if (dups)
		Con_Printf ("\neliminated %d duplicate names\n", dups);
	Con_Printf ("\n");

	// Free memory
	for (i = 0; i < cnt; i++)
		free (maplist[i]);

	free (maplist);
}
#endif

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
===========
*/
int file_from_pak;	// global indicating file came from pack file ZOID
size_t COM_FOpenFile (char *filename, FILE **file, qboolean override_pack)
{
	searchpath_t	*search;
	char		netpath[MAX_OSPATH];
	pack_t		*pak;
	int			i;

	file_from_pak = 0;

//
// search through the path, one element at a time
//
	for (search = com_searchpaths ; search ; search = search->next)
	{
	// is the element a pak file?
		if (search->pack)
		{
		// look through all the pak file elements
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
				if (!strcmp (pak->files[i].name, filename))
				{	// found it!
					// open a new file on the pakfile
					*file = fopen (pak->filename, "rb");
					if (!*file)
						Sys_Error ("Couldn't reopen %s", pak->filename);
					fseek (*file, pak->files[i].filepos, SEEK_SET);
					com_filesize = (size_t) pak->files[i].filelen;
					file_from_pak = 1;
					return com_filesize;
				}
		}
		else
		{
	// check a file in the directory tree
#ifndef H2W
			if (!(gameflags & GAME_REGISTERED) && !override_pack)
			{	// if not a registered version, don't ever go beyond base
				if ( strchr (filename, '/') || strchr (filename,'\\'))
					continue;
			}
#endif	// !H2W

			snprintf (netpath, sizeof(netpath), "%s/%s",search->filename, filename);
			if (access(netpath, R_OK) == -1)
				continue;

			*file = fopen (netpath, "rb");
			if (!*file)
				Sys_Error ("Couldn't reopen %s", netpath);
			return COM_filelength (*file);
		}
	}

	Sys_Printf ("FindFile: can't find %s\n", filename);

	*file = NULL;
	com_filesize = (size_t)-1;
	return com_filesize;
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte to the loaded data.
============
*/
#define	LOADFILE_ZONE		0
#define	LOADFILE_HUNK		1
#define	LOADFILE_TEMPHUNK	2
#define	LOADFILE_CACHE		3
#define	LOADFILE_STACK		4
#define	LOADFILE_BUF		5
#define	LOADFILE_MALLOC		6
static cache_user_t *loadcache;
static byte	*loadbuf;
static size_t		loadsize;

static byte *COM_LoadFile (char *path, int usehunk)
{
	FILE	*h;
	byte	*buf;
	char	base[32];
	size_t		len;

	buf = NULL;	// quiet compiler warning

// look for it in the filesystem or pack files
	len = com_filesize = COM_FOpenFile (path, &h, false);
	if (!h)
		return NULL;

// extract the filename base name for hunk tag
	COM_FileBase (path, base);

	switch (usehunk)
	{
	case LOADFILE_HUNK:
		buf = Hunk_AllocName (len+1, base);
		break;
	case LOADFILE_TEMPHUNK:
		buf = Hunk_TempAlloc (len+1);
		break;
	case LOADFILE_ZONE:
		buf = Z_Malloc (len+1);
		break;
	case LOADFILE_CACHE:
		buf = Cache_Alloc (loadcache, len+1, base);
		break;
	case LOADFILE_STACK:
		if (len + 1 > loadsize)
			buf = Hunk_TempAlloc (len+1);
		else
			buf = loadbuf;
		break;
	case LOADFILE_BUF:
		// Pa3PyX: like 4, except uses hunk (not temp) if no space
		if (len + 1 > loadsize)
			buf = Hunk_AllocName(len + 1, path);
		else
			buf = loadbuf;
		break;
	case LOADFILE_MALLOC:
		buf = Q_malloc (len+1);
		break;
	default:
		Sys_Error ("COM_LoadFile: bad usehunk");
	}

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

#if !defined(SERVERONLY) && !defined(GLQUAKE)
	Draw_BeginDisc ();
#endif
	fread (buf, 1, len, h);
	fclose (h);
#if !defined(SERVERONLY) && !defined(GLQUAKE)
	Draw_EndDisc ();
#endif
	return buf;
}

byte *COM_LoadHunkFile (char *path)
{
	return COM_LoadFile (path, LOADFILE_HUNK);
}

byte *COM_LoadZoneFile (char *path)
{
	return COM_LoadFile (path, LOADFILE_ZONE);
}

byte *COM_LoadTempFile (char *path)
{
	return COM_LoadFile (path, LOADFILE_TEMPHUNK);
}

void COM_LoadCacheFile (char *path, struct cache_user_s *cu)
{
	loadcache = cu;
	COM_LoadFile (path, LOADFILE_CACHE);
}

// uses temp hunk if larger than bufsize
byte *COM_LoadStackFile (char *path, void *buffer, size_t bufsize)
{
	byte	*buf;

	loadbuf = (byte *)buffer;
	loadsize = bufsize;
	buf = COM_LoadFile (path, LOADFILE_STACK);

	return buf;
}

// Pa3PyX: Like COM_LoadStackFile, excepts loads onto
// the hunk (instead of temp) if there is no space
byte *COM_LoadBufFile (char *path, void *buffer, size_t *bufsize)
{
	byte	*buf;

	loadbuf = (byte *)buffer;
	loadsize = (*bufsize) + 1;
	buf = COM_LoadFile (path, LOADFILE_BUF);
	if (buf && !(*bufsize))
		*bufsize = com_filesize;

	return buf;
}

// LordHavoc: returns malloc'd memory
byte *COM_LoadMallocFile (char *path)
{
	return COM_LoadFile (path, LOADFILE_MALLOC);
}

/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *COM_LoadPackFile (char *packfile, int paknum, qboolean base_fs)
{
	dpackheader_t	header;
	int				i;
	packfile_t		*newfiles;
	int				numpackfiles;
	pack_t			*pack;
	FILE			*packhandle;
	dpackfile_t		info[MAX_FILES_IN_PACK];
	unsigned short		crc;

	packhandle = fopen (packfile, "rb");
	if (!packhandle)
		return NULL;

	fread (&header, 1, sizeof(header), packhandle);
	if (header.id[0] != 'P' || header.id[1] != 'A' ||
	    header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	numpackfiles = header.dirlen / sizeof(dpackfile_t);

	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);

	newfiles = Z_Malloc (numpackfiles * sizeof(packfile_t));

	fseek (packhandle, header.dirofs, SEEK_SET);
	fread (&info, 1, header.dirlen, packhandle);

// crc the directory
	CRC_Init (&crc);
	for (i = 0; i < header.dirlen; i++)
		CRC_ProcessByte (&crc, ((byte *)info)[i]);

// check for modifications
	if (base_fs && paknum <= MAX_PAKDATA-2)
	{
		if (strcmp(gamedirfile, dirdata[paknum]) != 0)
		{
			// raven didnt ship like that
			gameflags |= GAME_MODIFIED;
		}
		else if (numpackfiles != pakdata[paknum][0])
		{
			if (paknum == 0)
			{
				// demo ??
				if (numpackfiles != pakdata[MAX_PAKDATA-1][0])
				{
				// not original
					gameflags |= GAME_MODIFIED;
				}
				else if (crc != pakdata[MAX_PAKDATA-1][1])
				{
				// not original
					gameflags |= GAME_MODIFIED;
				}
				else
				{
				// both crc and numfiles matched the demo
					gameflags |= GAME_DEMO;
				}
			}
			else
			{
			// not original
				gameflags |= GAME_MODIFIED;
			}
		}
		else if (crc != pakdata[paknum][1])
		{
		// not original
			gameflags |= GAME_MODIFIED;
		}
		else
		{
			switch (paknum)
			{
			case 0:	// pak0 of full version 1.11
				gameflags |= GAME_REGISTERED0;
				break;
			case 1:	// pak1 of full version 1.11
				gameflags |= GAME_REGISTERED1;
				break;
			case 2:	// bundle version
				gameflags |= GAME_OEM;
				break;
			case 3:	// mission pack
				gameflags |= GAME_PORTALS;
				break;
			case 4:	// hexenworld
				gameflags |= GAME_HEXENWORLD;
				break;
			default:// we shouldn't reach here
				break;
			}
		}
		// both crc and numfiles are good, we are still original
	}
	else
	{
		gameflags |= GAME_MODIFIED;
	}

// parse the directory
	for (i = 0; i < numpackfiles; i++)
	{
		Q_strlcpy_err(newfiles[i].name, info[i].name, MAX_QPATH);
		newfiles[i].filepos = LittleLong(info[i].filepos);
		newfiles[i].filelen = LittleLong(info[i].filelen);
	}

	pack = Z_Malloc (sizeof (pack_t));
	Q_strlcpy_err(pack->filename, packfile, MAX_OSPATH);
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}


/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ... 
================
*/
static void COM_AddGameDirectory (char *dir, qboolean base_fs)
{
	int				i;
	searchpath_t		*search;
	pack_t			*pak;
	char			pakfile[MAX_OSPATH];
	char			*p;
	qboolean		been_here = false;

	if ((p = strrchr(dir, '/')) != NULL)
	{
		Q_strlcpy_err(gamedirfile, ++p, sizeof(gamedirfile));
	}
	else
	{
		Q_strlcpy_err(gamedirfile, p, sizeof(gamedirfile));
	}
	Q_strlcpy_err(com_gamedir, dir, sizeof(com_gamedir));

//
// add any pak files in the format pak0.pak pak1.pak, ...
//
#ifdef PLATFORM_UNIX
add_pakfile:
#endif
	for (i = 0; i < 10; i++)
	{
		if (been_here)
		{
			Q_snprintf_err(pakfile, sizeof(pakfile), "%s/pak%i.pak", com_userdir, i);
		}
		else
		{
			Q_snprintf_err(pakfile, sizeof(pakfile), "%s/pak%i.pak", dir, i);
		}
		pak = COM_LoadPackFile (pakfile, i, base_fs);
		if (!pak)
			continue;
		search = Hunk_AllocName (sizeof(searchpath_t), "searchpath");
		search->pack = pak;
		search->next = com_searchpaths;
		com_searchpaths = search;
	}

// add the directory to the search path
// O.S: this needs to be done ~after~ adding the pakfiles in
// this dir, so that the dir itself will be placed above the
// pakfiles in the search order which, in turn, will allow
// override files:
// this way, data1/default.cfg will be opened instead of
// data1/pak0.pak:/default.cfg
	search = Hunk_AllocName (sizeof(searchpath_t), "searchpath");
	if (been_here)
	{
		Q_strlcpy_err(search->filename, com_userdir, MAX_OSPATH);
	}
	else
	{
		Q_strlcpy_err(search->filename, dir, MAX_OSPATH);
	}
	search->next = com_searchpaths;
	com_searchpaths = search;

	if (been_here)
		return;
	been_here = true;

// add user's directory to the search path
// add any pak files in the user's directory
#ifdef PLATFORM_UNIX
	if (strcmp(com_gamedir, com_userdir))
		goto add_pakfile;
#endif
}

/*
================
COM_Gamedir

Sets the gamedir and path to a different directory.

Hexen2 uses this for setting the gamedir upon seeing
a -game commandline argument. In addition to this,
hexenworld uses this procedure to set the gamedir on
both server and client sides during game execution:
Client calls this upon every map change from within
CL_ParseServerData() and the Server calls this upon
a gamedir command from within SV_Gamedir_f().
================
*/
void COM_Gamedir (char *dir)
{
	searchpath_t	*search, *next;
	int				i;
	pack_t			*pak;
	char			pakfile[MAX_OSPATH];
	qboolean		been_here = false;

	if (strstr(dir, "..") || strstr(dir, "/")
		|| strstr(dir, "\\") || strstr(dir, ":") )
	{
		Sys_Printf ("Gamedir should be a single directory name, not a path\n");
		return;
	}

	if (!Q_strcasecmp(gamedirfile, dir))
		return;		// still the same
	Q_strlcpy_err(gamedirfile, dir, sizeof(gamedirfile));

	// FIXME: Should I check for directory's existence ??

//
// free up any current game dir info: our top searchpath dir will be hw
// and any gamedirs set before by this very procedure will be removed.
// since hexen2 doesn't use this during game execution there will be no
// changes for it: it has portals or data1 at the top.
//
	while (com_searchpaths != com_base_searchpaths)
	{
		if (com_searchpaths->pack)
		{
			fclose (com_searchpaths->pack->handle);
			Z_Free (com_searchpaths->pack->files);
			Z_Free (com_searchpaths->pack);
		}
		next = com_searchpaths->next;
		Z_Free (com_searchpaths);
		com_searchpaths = next;
	}

//
// flush all data, so it will be forced to reload
//
	Cache_Flush ();

// check for reserved gamedirs
	if (!Q_strcasecmp(dir, "hw"))
	{
#if !defined(H2W)
	// hw is reserved for hexenworld only. hexen2 shouldn't use it
		Sys_Printf ("WARNING: Gamedir not set to hw :\n"
			    "It is reserved for HexenWorld.\n");
#else
	// that we reached here means the hw server decided to abandon
	// whatever the previous mod it was running and went back to
	// pure hw. weird.. do as he wishes anyway and adjust our variables.
		Q_snprintf_err(com_gamedir, sizeof(com_gamedir), "%s/hw", com_basedir);
#    ifdef PLATFORM_UNIX
		Q_snprintf_err(com_userdir, sizeof(com_userdir), "%s/hw", host_parms.userdir);
#    else
		Q_strlcpy_err (com_userdir, com_gamedir, sizeof(com_userdir));
#    endif
#    if defined(SERVERONLY)
	// change the *gamedir serverinfo properly
		Info_SetValueForStarKey (svs.info, "*gamedir", "hw", MAX_SERVERINFO_STRING);
#    endif
		Q_strlcpy_err (com_savedir, com_userdir, sizeof(com_savedir));
#endif
		return;
	}
	else if (!Q_strcasecmp(dir, "portals"))
	{
	// no hw server is supposed to set gamedir to portals
	// and hw must be above portals in hierarchy. this is
	// actually a hypothetical case.
	// as for hexen2, it cannot reach here.
		return;
	}
	else if (!Q_strcasecmp(dir, "data1"))
	{
	// another hypothetical case: no hw mod is supposed to
	// do this and hw must stay above data1 in hierarchy.
	// as for hexen2, it can only reach here by a silly
	// command line argument like -game data1, ignore it.
		return;
	}
	else
	{
	// a new gamedir: let's set it here.
		Q_snprintf_err(com_gamedir, sizeof(com_gamedir), "%s/%s", com_basedir, dir);
	}

//
// add any pak files in the format pak0.pak pak1.pak, ...
//
#ifdef PLATFORM_UNIX
add_pakfiles:
#endif
	for (i = 0; i < 10; i++)
	{
		if (been_here)
		{
			Q_snprintf_err(pakfile, sizeof(pakfile), "%s/pak%i.pak", com_userdir, i);
		}
		else
		{
			Q_snprintf_err(pakfile, sizeof(pakfile), "%s/pak%i.pak", com_gamedir, i);
		}
		pak = COM_LoadPackFile (pakfile, i, false);
		if (!pak)
			continue;
		search = Z_Malloc (sizeof(searchpath_t));
		search->pack = pak;
		search->next = com_searchpaths;
		com_searchpaths = search;
	}

// add the directory to the search path
// O.S: this needs to be done ~after~ adding the pakfiles in
// this dir, so that the dir itself will be placed above the
// pakfiles in the search order
	search = Z_Malloc (sizeof(searchpath_t));
	if (been_here)
	{
		Q_strlcpy_err(search->filename, com_userdir, MAX_OSPATH);
	}
	else
	{
		Q_strlcpy_err(search->filename, com_gamedir, MAX_OSPATH);
	}
	search->next = com_searchpaths;
	com_searchpaths = search;

	if (been_here)
		return;
	been_here = true;

#if defined(H2W) && defined(SERVERONLY)
// change the *gamedir serverinfo properly
	Info_SetValueForStarKey (svs.info, "*gamedir", dir, MAX_SERVERINFO_STRING);
#endif

// add user's directory to the search path
#ifdef PLATFORM_UNIX
	Q_snprintf_err(com_userdir, sizeof(com_userdir), "%s/%s", host_parms.userdir, dir);
	Sys_mkdir_err (com_userdir);
	Q_strlcpy_err (com_savedir, com_userdir, sizeof(com_savedir));
// add any pak files in the user's directory
	if (strcmp(com_gamedir, com_userdir))
		goto add_pakfiles;
#else
	Q_strlcpy_err (com_userdir, com_gamedir, sizeof(com_userdir));
	Q_strlcpy_err (com_savedir, com_userdir, sizeof(com_savedir));
#endif
}

/*
============
MoveUserData
moves all <userdir>/userdata to <userdir>/data1/userdata

AoT and earlier versions of HoT didn't create <userdir>/data1
and kept all user the data in <userdir> instead. Starting with
HoT 1.4.1, we are creating and using <userdir>/data1 . This
procedure is intended to update the user direcory accordingly.
Call from COM_InitFilesystem ~just after~ setting com_userdir
to host_parms.userdir/data1
============
*/
#ifdef PLATFORM_UNIX
static void do_movedata (char *path1, char *path2, FILE *logfile)
{
	Sys_Printf ("%s -> %s : ", path1, path2);
	if (logfile)
		fprintf (logfile, "%s -> %s : ", path1, path2);
	if (rename (path1, path2) == 0)
	{
		Sys_Printf("OK\n");
		if (logfile)
			fprintf(logfile, "OK\n");
	}
	else
	{
		Sys_Printf("Failed (%s)\n", strerror(errno));
		if (logfile)
			fprintf(logfile, "Failed (%s)\n", strerror(errno));
	}
}

static void MoveUserData (void)
{
	int		i;
	FILE		*fh;
	struct stat	test;
	char	*tmp, tmp1[MAX_OSPATH], tmp2[MAX_OSPATH];
	char	*movefiles[] = 
	{
		"*.cfg",	// config files
		"*.rc",		// config files
		"*.dem",	// pre-recorded demos
		"pak?.pak"	// pak files
	};
	char	*movedirs[] = 
	{
		"quick",	// quick saves
		"shots",	// screenshots
		".midi",	// midi cache
		"glhexen",	// model mesh cache
		/* these are highly unlikely, but just in case.. */
		"maps",
		"midi",
		"sound",
		"models",
		"gfx"
	};
#	define NUM_MOVEFILES	(sizeof(movefiles)/sizeof(movefiles[0]))
#	define NUM_MOVEDIRS	(sizeof(movedirs)/sizeof(movedirs[0]))

	Q_snprintf_err(tmp1, sizeof(tmp1), "%s/userdata.moved", com_userdir);
	if (stat(tmp1, &test) == 0)
	{
		// the data should have already been moved in earlier runs.
		if ((test.st_mode & S_IFREG) == S_IFREG)
			return;
	}
	fh = fopen(tmp1, "wb");

	Sys_Printf ("Moving user data from root of userdir to userdir/data1\n");

	for (i = 0; i < NUM_MOVEFILES; i++)
	{
		tmp = Sys_FindFirstFile (host_parms.userdir, movefiles[i]);
		while (tmp)
		{
			Q_snprintf_err(tmp1, sizeof(tmp1), "%s/%s", host_parms.userdir, tmp);
			Q_snprintf_err(tmp2, sizeof(tmp2), "%s/%s", com_userdir, tmp);
			do_movedata (tmp1, tmp2, fh);
			tmp = Sys_FindNextFile ();
		}
		Sys_FindClose ();
	}

	// move the savegames
	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		Q_snprintf_err(tmp1, sizeof(tmp1), "%s/s%d", host_parms.userdir, i);
		if (stat(tmp1, &test) == 0)
		{
			if ((test.st_mode & S_IFDIR) == S_IFDIR)
			{
				Q_snprintf_err(tmp2, sizeof(tmp2), "%s/s%d", com_userdir, i);
				do_movedata (tmp1, tmp2, fh);
			}
		}
	}

	// move the savegames (multiplayer)
	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		Q_snprintf_err(tmp1, sizeof(tmp1), "%s/ms%d", host_parms.userdir, i);
		if (stat(tmp1, &test) == 0)
		{
			if ((test.st_mode & S_IFDIR) == S_IFDIR)
			{
				Q_snprintf_err(tmp2, sizeof(tmp2), "%s/ms%d", com_userdir, i);
				do_movedata (tmp1, tmp2, fh);
			}
		}
	}

	// other dirs
	for (i = 0; i < NUM_MOVEDIRS; i++)
	{
		Q_snprintf_err(tmp1, sizeof(tmp1), "%s/%s", host_parms.userdir, movedirs[i]);
		if (stat(tmp1, &test) == 0)
		{
			if ((test.st_mode & S_IFDIR) == S_IFDIR)
			{
				Q_snprintf_err(tmp2, sizeof(tmp2), "%s/%s", com_userdir, movedirs[i]);
				do_movedata (tmp1, tmp2, fh);
			}
		}
	}

	if (fh)
		fclose (fh);
}
#endif

/*
================
COM_InitFilesystem
================
*/
static void COM_InitFilesystem (void)
{
	int		i;
	char		temp[12];
	qboolean	check_portals = false;
	searchpath_t	*search_tmp, *next_tmp;

//
// -basedir <path>
// Overrides the system supplied base directory (under data1)
//
	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc-1)
	{
		Q_strlcpy_err(com_basedir, com_argv[i+1], sizeof(com_basedir));
	}
	else
	{
		Q_strlcpy_err(com_basedir, host_parms.basedir, sizeof(com_basedir));
	}

	Q_strlcpy_err(com_userdir, host_parms.userdir, sizeof(com_userdir));

//
// start up with data1 by default
//
	Q_snprintf_err(com_userdir, sizeof(com_userdir), "%s/data1", host_parms.userdir);
#ifdef PLATFORM_UNIX
// properly move the user data from older versions in the user's directory
	Sys_mkdir_err (com_userdir);
	MoveUserData ();
#endif
	COM_AddGameDirectory (va("%s/data1", com_basedir), true);

	// check if we are playing the registered version
	COM_CheckRegistered ();
	// check for mix'n'match screw-ups
	if ((gameflags & GAME_REGISTERED) && ((gameflags & GAME_DEMO) || (gameflags & GAME_OEM)))
		Sys_Error ("Bad Hexen II installation");
#if !( defined(H2W) && defined(SERVERONLY) )
	if ((gameflags & GAME_MODIFIED) && !(gameflags & GAME_REGISTERED))
		Sys_Error ("You must have the full version of Hexen II to play modified games");
#endif

#if defined(H2MP) || defined(H2W)
	if (! COM_CheckParm ("-noportals"))
		check_portals = true;
#else
// see if the user wants mission pack support
	check_portals = (COM_CheckParm ("-portals")) || (COM_CheckParm ("-missionpack")) || (COM_CheckParm ("-h2mp"));
	i = COM_CheckParm ("-game");
	if (i && i < com_argc-1)
	{
		if (!Q_strcasecmp(com_argv[i+1], "portals"))
			check_portals = true;
	}
#endif

//	if (check_portals && !(gameflags & GAME_REGISTERED))
//		Sys_Error ("Portal of Praevus requires registered version of Hexen II");
	if (check_portals && (gameflags & GAME_REGISTERED))
	{
		i = Hunk_LowMark ();
		search_tmp = com_searchpaths;

		Q_snprintf_err(com_userdir, sizeof(com_userdir), "%s/portals", host_parms.userdir);
		Sys_mkdir_err (com_userdir);
		COM_AddGameDirectory (va("%s/portals", com_basedir), true);

		// back out searchpaths from invalid mission pack installations
		if ( !(gameflags & GAME_PORTALS))
		{
			Sys_Printf ("Missing or invalid mission pack installation\n");
			while (com_searchpaths != search_tmp)
			{
				if (com_searchpaths->pack)
				{
					fclose (com_searchpaths->pack->handle);
					Sys_Printf ("Removed packfile %s\n", com_searchpaths->pack->filename);
				}
				else
				{
					Sys_Printf ("Removed path %s\n", com_searchpaths->filename);
				}
				next_tmp = com_searchpaths->next;
				com_searchpaths = next_tmp;
			}
			com_searchpaths = search_tmp;
			Hunk_FreeToLowMark (i);
			// back to data1
			snprintf (com_gamedir, sizeof(com_gamedir), "%s/data1", com_basedir);
			snprintf (com_userdir, sizeof(com_userdir), "%s/data1", host_parms.userdir);
		}
	}

#if defined(H2W)
	Q_snprintf_err(com_userdir, sizeof(com_userdir), "%s/hw", host_parms.userdir);
	Sys_mkdir_err (com_userdir);
	COM_AddGameDirectory (va("%s/hw", com_basedir), true);
	// error out for H2W builds if GAME_HEXENWORLD isn't set
	if (!(gameflags & GAME_HEXENWORLD))
		Sys_Error ("You must have the HexenWorld data installed");
#endif

// this is the end of our base searchpath:
// any set gamedirs, such as those from -game commandline
// arguments, from exec'ed configs or the ones dictated by
// the server, will be freed up to here upon a new gamedir
// command
	com_base_searchpaths = com_searchpaths;

	Q_strlcpy_err(com_savedir, com_userdir, sizeof(com_savedir));

	i = COM_CheckParm ("-game");
	if (i && !(gameflags & GAME_REGISTERED))
	{
	// only registered versions can do -game
		Sys_Error ("You must have the full version of Hexen II to play modified games");
	}
	else
	{
	// add basedir/gamedir as an override game
		if (i && i < com_argc-1)
			COM_Gamedir (com_argv[i+1]);
	}

// finish the filesystem setup
	oem.flags &= ~CVAR_ROM;
	registered.flags &= ~CVAR_ROM;
	if (gameflags & GAME_REGISTERED)
	{
		snprintf (temp, sizeof(temp), "registered");
		Cvar_Set ("registered", "1");
	}
	else if (gameflags & GAME_OEM)
	{
		snprintf (temp, sizeof(temp), "oem");
		Cvar_Set ("oem", "1");
	}
	else if (gameflags & GAME_DEMO)
	{
		snprintf (temp, sizeof(temp), "demo");
	}
	else
	{
	//	snprintf (temp, sizeof(temp), "unknown");
	// no proper Raven data: it's best to error out here
		Sys_Error ("Unable to find a proper Hexen II installation");
	}
	oem.flags |= CVAR_ROM;
	registered.flags |= CVAR_ROM;

	Sys_Printf ("Playing %s version.\n", temp);
}

/*
============
COM_FileInGamedir

Reports the existance of a file with read
permissions in com_gamedir or com_userdir.
-1 is returned on failure, ie. the return
value of the access() function
Files in pakfiles are NOT meant for this
procedure!
============
*/
int COM_FileInGamedir (char *fname)
{
	int	ret;

	ret = access (va("%s/%s", com_userdir, fname), R_OK);
	if (ret == -1)
		ret = access (va("%s/%s", com_gamedir, fname), R_OK);

	return ret;
}


/*
=====================================================================

  INFO STRINGS

=====================================================================
*/
#ifdef H2W
/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
===============
*/
char *Info_ValueForKey (char *s, char *key)
{
	char	pkey[512];
	static	char value[4][512];	// use two buffers so compares
								// work without stomping on each other
	static	int	valueindex;
	char	*o;
	
	valueindex = (valueindex + 1) % 4;
	if (*s == '\\')
		s++;
	while (1)
	{
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return "";
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value[valueindex];

		while (*s != '\\' && *s)
		{
			if (!*s)
				return "";
			*o++ = *s++;
		}
		*o = 0;

		if (!strcmp (key, pkey) )
			return value[valueindex];

		if (!*s)
			return "";
		s++;
	}
}

void Info_RemoveKey (char *s, char *key)
{
	char	*start;
	char	pkey[512];
	char	value[512];
	char	*o;

	if (strstr (key, "\\"))
	{
		Con_Printf ("Can't use a key with a \\\n");
		return;
	}

	while (1)
	{
		start = s;
		if (*s == '\\')
			s++;
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;
		while (*s != '\\' && *s)
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;

		if (!strcmp (key, pkey) )
		{
			strcpy (start, s);	// remove this part
			return;
		}

		if (!*s)
			return;
	}

}

void Info_RemovePrefixedKeys (char *start, char prefix)
{
	char	*s;
	char	pkey[512];
	char	value[512];
	char	*o;

	s = start;

	while (1)
	{
		if (*s == '\\')
			s++;
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;
		while (*s != '\\' && *s)
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;

		if (pkey[0] == prefix)
		{
			Info_RemoveKey (start, pkey);
			s = start;
		}

		if (!*s)
			return;
	}

}

void Info_SetValueForStarKey (char *s, char *key, char *value, int maxsize)
{
	char	new[1024], *v;
	int		c;
#ifdef SERVERONLY
	extern cvar_t sv_highchars;
#endif

	if (strstr (key, "\\") || strstr (value, "\\") )
	{
		Con_Printf ("Can't use keys or values with a \\\n");
		return;
	}

	if (strstr (key, "\"") || strstr (value, "\"") )
	{
		Con_Printf ("Can't use keys or values with a \"\n");
		return;
	}

	if (strlen(key) > 63 || strlen(value) > 63)
	{
		Con_Printf ("Keys and values must be < 64 characters.\n");
		return;
	}
	Info_RemoveKey (s, key);
	if (!value || !strlen(value))
		return;

	sprintf (new, "\\%s\\%s", key, value);

	if (strlen(new) + strlen(s) > maxsize)
	{
		Con_Printf ("Info string length exceeded\n");
		return;
	}

	// only copy ascii values
	s += strlen(s);
	v = new;
	while (*v)
	{
		c = (unsigned char)*v++;
#ifndef SERVERONLY
		// client only allows highbits on name
		if (Q_strcasecmp(key, "name") != 0)
		{
			c &= 127;
			if (c < 32 || c > 127)
				continue;
			// auto lowercase team
			if (Q_strcasecmp(key, "team") == 0)
				c = tolower(c);
		}
#else
		if (!sv_highchars.value)
		{
			c &= 127;
			if (c < 32 || c > 127)
				continue;
		}
#endif
//		c &= 127;		// strip high bits
		if (c > 13) // && c < 127)
			*s++ = c;
	}
	*s = 0;
}

void Info_SetValueForKey (char *s, char *key, char *value, int maxsize)
{
	if (key[0] == '*')
	{
		Con_Printf ("Can't set * keys\n");
		return;
	}

	Info_SetValueForStarKey (s, key, value, maxsize);
}

void Info_Print (char *s)
{
	char	key[512];
	char	value[512];
	char	*o;
	int		l;

	if (*s == '\\')
		s++;
	while (*s)
	{
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = o - key;
		if (l < 20)
		{
			memset (o, ' ', 20-l);
			key[20] = 0;
		}
		else
			*o = 0;
		Con_Printf ("%s", key);

		if (!*s)
		{
			Con_Printf ("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = 0;

		if (*s)
			s++;
		Con_Printf ("%s\n", value);
	}
}
#endif	// H2W
