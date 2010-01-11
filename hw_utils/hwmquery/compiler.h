/*
	compiler.h
	compiler specific definitions and settings
	used in the uhexen2 (Hammer of Thyrion) tree.
	- standalone header
	- doesn't and must not include any other headers
	- shouldn't depend on arch_def.h, q_stdinc.h, or
	  any other headers

	$Id: compiler.h,v 1.15 2010-01-11 18:48:19 sezero Exp $

	Copyright (C) 2007  O.Sezer <sezero@users.sourceforge.net>

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		51 Franklin St, Fifth Floor,
		Boston, MA  02110-1301  USA
*/

#ifndef __HX2_COMPILER_H
#define __HX2_COMPILER_H

#if !defined(__GNUC__)
#define	__attribute__(x)
#endif	/* __GNUC__ */

/* argument format attributes for function
 * pointers are supported for gcc >= 3.1
 */
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 0))
#define	__fp_attribute__	__attribute__
#else
#define	__fp_attribute__(x)
#endif

/* function optimize attribute is added
 * starting with gcc 4.4.0
 */
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ > 3))
#define	__no_optimize		__attribute__((__optimize__("0")))
#else
#define	__no_optimize
#endif

#if !(defined(__GNUC__) &&  (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8)))
#define __extension__
#endif	/* __GNUC__ */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define	__thisfunc__	__func__
#elif defined(__GNUC__) && __GNUC__ < 3
#define	__thisfunc__	__FUNCTION__
#elif defined(__GNUC__) && __GNUC__ > 2
#define	__thisfunc__	__func__
#elif defined(__WATCOMC__)
#define	__thisfunc__	__FUNCTION__
#elif defined(__LCC__)
#define	__thisfunc__	__func__
#elif defined(_MSC_VER) && _MSC_VER >= 1300	/* VC7++ */
#define	__thisfunc__	__FUNCTION__
#else	/* stupid fallback */
/*#define	__thisfunc__	__FILE__*/
#error	__func__ or __FUNCTION__ compiler token not supported? define one...
#endif

/* Some compilers, such as OpenWatcom, and possibly other compilers
 * from the DOS universe, define __386__ instead of __i386__
 */
#if defined(__386__) && !defined(__i386__)
#define __i386__		1
#endif

/* inline keyword: */
#if defined(_MSC_VER) && !defined(__cplusplus)
#define inline __inline
#endif	/* _MSC_VER */


#endif	/* __HX2_COMPILER_H */

