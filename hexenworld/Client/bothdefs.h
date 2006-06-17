/*
	bothdefs.h
	defs common to both client and server

	$Header: /home/ozzie/Download/0000/uhexen2/hexenworld/Client/bothdefs.h,v 1.39 2006-06-17 19:54:54 sezero Exp $
*/

#define __STRINGIFY(x) #x
#define STRINGIFY(x) __STRINGIFY(x)

#include "arch_def.h"

#define	HOT_VERSION_MAJ		1
#define	HOT_VERSION_MID		4
#define	HOT_VERSION_MIN		1
#define	HOT_VERSION_REL_DATE	"2006-06-17"
#define	HOT_VERSION_BETA	1
#define	HOT_VERSION_BETA_STR	"pre6"
#define	HOT_VERSION_STR		STRINGIFY(HOT_VERSION_MAJ) "." STRINGIFY(HOT_VERSION_MID) "." STRINGIFY(HOT_VERSION_MIN)
#define	GLQUAKE_VERSION		1.00
#define	ENGINE_VERSION		0.17
#define	ENGINE_NAME		"HexenWorld"

#ifndef	DEMOBUILD
#ifdef __MACOSX__
#define	AOT_USERDIR		"Library/Application Support/Hexen2"
#else
#define	AOT_USERDIR		".hexen2"
#endif
#else
#ifdef __MACOSX__
#define	AOT_USERDIR		"Library/Application Support/Hexen2 Demo"
#else
#define	AOT_USERDIR		".hexen2demo"
#endif
#endif

#define	MAX_QPATH	64	// max length of a quake game pathname
#define	MAX_OSPATH	256	// max length of a filesystem pathname

#define	QUAKE_GAME		// as opposed to utilities
//define	PARANOID	// speed sapping error checking

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
#define CACHE_SIZE	32	// used to align key data structures

#define UNUSED(x)	(x = x)	// for pesky compiler / lint warnings

#define	MINIMUM_MEMORY	0x550000

#define MAX_NUM_ARGVS	50

// up / down
#define	PITCH		0

// left / right
#define	YAW		1

// fall over
#define	ROLL		2


//
// Timing macros
//
#define HX_FRAME_TIME	0.05
#define HX_FPS		20


#define	MAX_SCOREBOARD	32		// max numbers of players

#define	SOUND_CHANNELS	8


#define	ON_EPSILON	0.1		// point on plane side epsilon

#define	MAX_MSGLEN	7500		// max length of a reliable message
#define	MAX_DATAGRAM	1400		// max length of unreliable message

//
// per-level limits
//
#define	MAX_EDICTS	768		// FIXME: ouch! ouch! ouch!
#define	MAX_LIGHTSTYLES	64
#define	MAX_MODELS	512		// Sent over the net as a word
#define	MAX_SOUNDS	256		// so they cannot be blindly increased

#define	SAVEGAME_COMMENT_LENGTH	39

#define	MAX_STYLESTRING		64

//
// stats are integers communicated to the client by the server
//
#define	MAX_CL_STATS		32
#define	STAT_HEALTH		0
//define	STAT_FRAGS		1
#define	STAT_WEAPON		2
#define	STAT_AMMO		3
#define	STAT_ARMOR		4
//define	STAT_WEAPONFRAME	5
#define	STAT_SHELLS		6
#define	STAT_NAILS		7
#define	STAT_ROCKETS		8
#define	STAT_CELLS		9
#define	STAT_ACTIVEWEAPON	10
#define	STAT_TOTALSECRETS	11
#define	STAT_TOTALMONSTERS	12
#define	STAT_SECRETS		13	// bumped on client side by svc_foundsecret
#define	STAT_MONSTERS		14	// bumped by svc_killedmonster
#define	STAT_ITEMS		15
//define	STAT_VIEWHEIGHT		16

#define	MAX_INVENTORY		15	// Max inventory array size

#define	MAX_SAVEGAMES		12	// max number of savegames


//
// item flags
//
#define	IT_SHOTGUN		1
#define	IT_SUPER_SHOTGUN	2
#define	IT_NAILGUN		4
#define	IT_SUPER_NAILGUN	8
#define	IT_GRENADE_LAUNCHER	16
#define	IT_ROCKET_LAUNCHER	32
#define	IT_LIGHTNING		64
#define	IT_SUPER_LIGHTNING	128
#define	IT_SHELLS		256
#define	IT_NAILS		512
#define	IT_ROCKETS		1024
#define	IT_CELLS		2048
#define	IT_AXE			4096
#define	IT_ARMOR1		8192
#define	IT_ARMOR2		16384
#define	IT_ARMOR3		32768
#define	IT_SUPERHEALTH		65536
#define	IT_KEY1			131072
#define	IT_KEY2			262144
#define	IT_INVISIBILITY		524288
#define	IT_INVULNERABILITY	1048576
#define	IT_SUIT			2097152
#define	IT_QUAD			4194304
#define	IT_SIGIL1		(1 << 28)
#define	IT_SIGIL2		(1 << 29)
#define	IT_SIGIL3		(1 << 30)
#define	IT_SIGIL4		(1 << 31)

#define	ART_HASTE			1
#define	ART_INVINCIBILITY		2
#define	ART_TOMEOFPOWER			4
#define	ART_INVISIBILITY		8
#define	ARTFLAG_FROZEN			16
#define	ARTFLAG_STONED			32
#define	ARTFLAG_DIVINE_INTERVENTION	64
#define	ARTFLAG_BOOTS			128

//
// edict->drawflags
//
#define MLS_MASKIN			7	// MLS: Model Light Style
#define MLS_MASKOUT			248
#define MLS_NONE			0
#define MLS_FULLBRIGHT			1
#define MLS_POWERMODE			2
#define MLS_TORCH			3
#define MLS_TOTALDARK			4
#define MLS_ABSLIGHT			7
#define SCALE_TYPE_MASKIN		24
#define SCALE_TYPE_MASKOUT		231
#define SCALE_TYPE_UNIFORM		0	// Scale X, Y, and Z
#define SCALE_TYPE_XYONLY		8	// Scale X and Y
#define SCALE_TYPE_ZONLY		16	// Scale Z
#define SCALE_ORIGIN_MASKIN		96
#define SCALE_ORIGIN_MASKOUT		159
#define SCALE_ORIGIN_CENTER		0	// Scaling origin at object center
#define SCALE_ORIGIN_BOTTOM		32	// Scaling origin at object bottom
#define SCALE_ORIGIN_TOP		64	// Scaling origin at object top
#define DRF_TRANSLUCENT			128

//
// game data flags
//
#define	GAME_DEMO		1
#define	GAME_OEM		2
#define	GAME_MODIFIED		4
#define	GAME_REGISTERED		8
#define	GAME_REGISTERED0	16
#define	GAME_REGISTERED1	32
#define	GAME_PORTALS		64
#define	GAME_HEXENWORLD		128

//
// Player Classes
//
#define MAX_PLAYER_CLASS	6
#define ABILITIES_STR_INDEX	400

#define CLASS_PALADIN		1
#define CLASS_CLERIC 		2
#define CLASS_NECROMANCER	3
#define CLASS_THEIF   		4
#define CLASS_DEMON		5
#define CLASS_DWARF		6

//
//Siege teams
//
#define ST_DEFENDER		1
#define ST_ATTACKER   		2

//
//Dm Modes
//
#define DM_CAPTURE_THE_TOKEN	1
#define DM_HUNTER		2
#define DM_SIEGE		3

//
// print flags
//
#define	PRINT_LOW		0	// pickup messages
#define	PRINT_MEDIUM		1	// death messages
#define	PRINT_HIGH		2	// critical messages
#define	PRINT_CHAT		3	// chat messages
#define	PRINT_SOUND		4	// says a sound

