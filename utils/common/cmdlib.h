/*
	cmdlib.h

	$Id: cmdlib.h,v 1.12 2007-02-10 18:01:29 sezero Exp $
*/

#ifndef __CMDLIB_H__
#define __CMDLIB_H__

// HEADER FILES ------------------------------------------------------------

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
// for strcasecmp and strncasecmp
#include <strings.h>
#endif

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>


// TYPES -------------------------------------------------------------------

#ifndef __BYTEBOOL__
#define __BYTEBOOL__
typedef enum { false, true } qboolean;
typedef unsigned char byte;
#endif


// MACROS ------------------------------------------------------------------

#ifdef __GNUC__
#define _FUNC_PRINTF(n) __attribute__ ((format (printf, n, n+1)))
#else
#define _FUNC_PRINTF(n)
#endif

#ifdef _WIN32
#define Q_strncasecmp	strnicmp
#define Q_strcasecmp	stricmp
#else
#define Q_strncasecmp	strncasecmp
#define Q_strcasecmp	strcasecmp
#endif

#if defined(_WIN32) && !defined(F_OK)
// values for the second argument to access(). MS does not define them
#define	R_OK	4		/* Test for read permission.  */
#define	W_OK	2		/* Test for write permission.  */
#define	X_OK	1		/* Test for execute permission.  */
#define	F_OK	0		/* Test for existence.  */
#endif

// the dec offsetof macro doesn't work very well...
#define myoffsetof(type,identifier) ((size_t)&((type *)0)->identifier)


// strlcpy and strlcat :
#include "strl_fn.h"

#if HAVE_STRLCAT && HAVE_STRLCPY

// use native library functions
#define Q_strlcpy strlcpy
#define Q_strlcat strlcat

#else

// use our own copies of strlcpy and strlcat taken from OpenBSD :
extern size_t Q_strlcpy (char *dst, const char *src, size_t size);
extern size_t Q_strlcat (char *dst, const char *src, size_t size);

#endif

#define Q_strlcat_err(DST,SRC,SIZE) {								\
	if (Q_strlcat((DST),(SRC),(SIZE)) >= (SIZE))						\
		Error("%s:%d, strlcat: string buffer overflow!",__FUNCTION__,__LINE__);		\
}
#define Q_strlcpy_err(DST,SRC,SIZE) {								\
	if (Q_strlcpy((DST),(SRC),(SIZE)) >= (SIZE))						\
		Error("%s:%d, strlcpy: string buffer overflow!",__FUNCTION__,__LINE__);		\
}
#define Q_snprintf_err(DST,SIZE,fmt,args...) {							\
	if (snprintf((DST),(SIZE),fmt,##args) >= (SIZE))					\
		Error("%s:%d, snprintf: string buffer overflow!",__FUNCTION__,__LINE__);	\
}


// PUBLIC DATA DECLARATIONS ------------------------------------------------

// set these before calling CheckParm
extern int		myargc;
extern char		**myargv;

extern char	com_token[1024];
extern qboolean		com_eof;


// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

char	*Q_strlwr (char *str);
char	*Q_strupr (char *str);

/*
#ifndef _WIN32
int	Sys_kbhit(void);
#endif
*/

void	*SafeMalloc (size_t n, const char *desc);

void	Q_mkdir (const char *path);
void	Q_getwd (char *out);
int	Q_filelength (FILE *f);
int	Q_filetime (const char *path);

double	GetTime (void);

void	Error (const char *error, ...) _FUNC_PRINTF(1);
int	CheckParm (const char *check);

FILE	*SafeOpenWrite (const char *filename);
FILE	*SafeOpenRead (const char *filename);
void	SafeRead (FILE *f, void *buffer, int count);
void	SafeWrite (FILE *f, const void *buffer, int count);

int	LoadFile (const char *filename, void **bufferptr);
void	SaveFile (const char *filename, const void *buffer, int count);

void	DefaultExtension (char *path, const char *extension);
void	DefaultPath (char *path, const char *basepath);
void	StripFilename (char *path);
void	StripExtension (char *path);

void	ExtractFilePath (const char *path, char *dest);
void	ExtractFileBase (const char *path, char *dest);
void	ExtractFileExtension (const char *path, char *dest);

void	CreatePath (char *path);
void	Q_CopyFile (const char *from, const char *to);

int	ParseNum (const char *str);

char	*COM_Parse (char *data);

void	CRC_Init (unsigned short *crcvalue);
void	CRC_ProcessByte (unsigned short *crcvalue, byte data);
unsigned short	CRC_Value (unsigned short crcvalue);

#define	HASH_TABLE_SIZE		9973
int	COM_Hash (const char *string);

// endianness stuff: <sys/types.h> is supposed
// to succeed in locating the correct endian.h
// this BSD style may not work everywhere, eg. on WIN32
#if !defined(BYTE_ORDER) || !defined(LITTLE_ENDIAN) || !defined(BIG_ENDIAN) || (BYTE_ORDER != LITTLE_ENDIAN && BYTE_ORDER != BIG_ENDIAN)
#undef BYTE_ORDER
#undef LITTLE_ENDIAN
#undef BIG_ENDIAN
#define LITTLE_ENDIAN 1234
#define BIG_ENDIAN 4321
#endif
// assumptions in case we don't have endianness info
#ifndef BYTE_ORDER
#if defined(_WIN32)
#define BYTE_ORDER LITTLE_ENDIAN
#define GUESSED_WIN32_ENDIANNESS
#else
#if defined(SUNOS)
// these bits from darkplaces project
#define GUESSED_SUNOS_ENDIANNESS
#if defined(__i386) || defined(__amd64)
#define BYTE_ORDER LITTLE_ENDIAN
#else
#define BYTE_ORDER BIG_ENDIAN
#endif
#else
#define ASSUMED_LITTLE_ENDIAN
#define BYTE_ORDER LITTLE_ENDIAN
#endif
#endif
#endif

short	ShortSwap (short);
int	LongSwap (int);
float	FloatSwap (float);

#if BYTE_ORDER == BIG_ENDIAN
#define BigShort(s) (s)
#define LittleShort(s) ShortSwap((s))
#define BigLong(l) (l)
#define LittleLong(l) LongSwap((l))
#define BigFloat(f) (f)
#define LittleFloat(f) FloatSwap((f))
#else
// BYTE_ORDER == LITTLE_ENDIAN
#define BigShort(s) ShortSwap((s))
#define LittleShort(s) (s)
#define BigLong(l) LongSwap((l))
#define LittleLong(l) (l)
#define BigFloat(f) FloatSwap((f))
#define LittleFloat(f) (f)
#endif

// end of endianness stuff

#endif	/* __CMDLIB_H__	*/

