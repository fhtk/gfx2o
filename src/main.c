/* -*- coding: utf-8 -*- */
/****************************************************************************\
 *                                  gfx2o™                                  *
 *                                                                          *
 *                         Copyright © 2020 Aquefir                         *
 *                           Released under MPL2.                           *
\****************************************************************************/

#include <mangledeggs.h>
#include <stdio.h>
#include <unistd.h>
#include <uni/arr.h>
#include <uni/memory.h>
#include <uni/str.h>
#include <uni/types/int.h>

#include "excall.h"

static const char* helptxt =
	"PNG graphics to object code converter\n\nUsage:\n    \n    gfx2obj <input> [output]\n    Takes a PNG file <input>, runs it through grit, tags it with\n    the necessary symbols, and outputs an object code file.\n\ngfx2obj takes all of its metadata hints from the file\nextension provided. It uses this format (regex):\n\n    \\.[148](tn?|b)\\.(il?)?(ml?)?(pl?([0-9]{1,3})?)?\\.png$\n\nThe bpp portion specifies its bits-per-pixel, 4 or 8.\nThe next portion specifies what form the image takes on the GBA:\n";
static const char* helptxt2 =
	"tile or bitmap based. If \"tn\" is used, no tile reduction is done.\nThe next part specifies what kind of outputs to emit (i for\nimage/tileset, m for tilemap, and p for palette), and whether to\ncompress each output (l suffix, using LZ77). The optional numeric\nspecifies exactly how many colours the palette should have, instead\nof the maximum for the given bit depth.\n";

enum
{
	BPP_1 = 0,
	BPP_4,
	BPP_8
};

struct gfxprops
{
	u32 img : 1;
	u32 img_lz : 1;
	u32 map : 1;
	u32 map_lz : 1;
	u32 pal : 1;
	u32 pal_lz : 1;
	u32 reduce : 1;
	u32 tile : 1;
	u32 palsz : 8;
	u32 bpp : 2;
};

enum
{
	ERR_PARSE_EXT_TOO_FEW_DOTS = 1,
	ERR_PARSE_EXT_INVALID_OUT_TYPES,
	ERR_PARSE_EXT_INVALID_GFX_FORM,
	ERR_PARSE_EXT_INVALID_BPP
};

static int parse_ext( const char* fname, struct gfxprops* o )
{
	struct gfxprops out;
	char** spl;
	char tmpch;
	u32 spl_sz, tmpsz, tmpsz_wpal, i, fac, palsz;

	spl    = uni_strsplit( fname, ".", -1 );
	spl_sz = uni_strlenv( spl );

	if( spl_sz < 3 )
	{
		return ERR_PARSE_EXT_TOO_FEW_DOTS;
	}

	tmpsz = uni_strlen( spl[spl_sz - 2] );
	/* this is saved so we know how long the string with palette count is
	 */
	tmpsz_wpal = tmpsz;

	/* reduce until no more numbers are left on the end
	 * these numbers, if present, explicitly specify palette count */
	for( i = 0; spl[spl_sz - 2][tmpsz - 1] >= 0x30 &&
		spl[spl_sz - 2][tmpsz - 1] <= 0x39;
		++i )
	{
		--tmpsz;
	}

	/* parse the output selector chars */
	for( i = 0; i < tmpsz; ++i )
	{
		char doneletter = spl[spl_sz - 2][i];

		switch( spl[spl_sz - 2][i] )
		{
		case 'i':
			out.img = 1;
			break;
		case 'm':
			out.map = 1;
			break;
		case 'p':
			out.pal = 1;
			break;
		default:
			doneletter = '\0';

			return ERR_PARSE_EXT_INVALID_OUT_TYPES;
		case 'l':
			doneletter = '\0';
			/* this is handled outside this switchcase */
			break;
		}

		if( doneletter && i < tmpsz - 1 && spl[spl_sz - 2][i + 1] )
		{
			switch( doneletter )
			{
			case 'i':
				out.img_lz = 1;
				break;
			case 'm':
				out.map_lz = 1;
				break;
			case 'p':
				out.pal_lz = 1;
				break;
			default:
				break;
			}
		}
	}

	fac   = 1;
	palsz = 0;

	/* parse the palette size, if present */
	for( i = tmpsz_wpal - 1; i > 0 && i >= tmpsz; --i )
	{
		palsz += ( spl[spl_sz - 2][i] - 0x30 ) * fac;

		fac *= 10;
	}

	/* if there was no explicit palette size, default it */
	out.palsz = palsz == 0 ? 255 : palsz - 1;

	/* check size first */
	tmpsz = uni_strlen( spl[spl_sz - 3] );

	if( tmpsz < 2 )
	{
		return ERR_PARSE_EXT_INVALID_GFX_FORM;
	}

	/* get BPP */
	tmpch = spl[spl_sz - 3][0];

	out.bpp = tmpch == '1' ? BPP_1 : tmpch == '4' ? BPP_4 : BPP_8;

	if( out.bpp == BPP_8 && tmpch != '8' )
	{
		return ERR_PARSE_EXT_INVALID_BPP;
	}

	/* get form (bitmap or tile) */
	switch( spl[spl_sz - 3][1] )
	{
	case 'b':
		out.tile = 0;
		break;
	case 't':
		out.tile = 1;
		break;
	default:
		return ERR_PARSE_EXT_INVALID_GFX_FORM;
	}

	out.reduce = 1;

	if( out.tile && tmpsz >= 3 && spl[spl_sz - 3][2] == 'n' )
	{
		out.reduce = 0;
	}

	*o = out;

	return 0;
}

static char** mkgritflags(
	struct gfxprops props, const char* iname, const char* oname )
{
	struct uni_arr* flags;
	const char* temp;

	flags = uni_arr_init( sizeof( char* ) );

	if( props.img )
	{
		temp = uni_strdup( "-g" );
		uni_arr_app( flags, &temp );
		temp = uni_strdup( props.img_lz ? "-gzl" : "-gz!" );
		uni_arr_app( flags, &temp );
		temp = uni_strdup( props.tile ? "-gt" : "-gb" );
		uni_arr_app( flags, &temp );
		temp = uni_strdup( props.bpp == BPP_1
				? "-gB1"
				: props.bpp == BPP_4 ? "-gB4" : "-gB8" );
		uni_arr_app( flags, &temp );
	}
	else
	{
		temp = uni_strdup( "-g!" );
		uni_arr_app( flags, &temp );
	}

	if( props.map )
	{
		temp = uni_strdup( "-m" );
		uni_arr_app( flags, &temp );
		temp = uni_strdup( props.map_lz ? "-mzl" : "-mz!" );
		uni_arr_app( flags, &temp );
		temp = uni_strdup( props.reduce ? "-mRtf" : "-mR!" );
		uni_arr_app( flags, &temp );
	}
	else
	{
		temp = uni_strdup( "-m!" );
		uni_arr_app( flags, &temp );
		temp = uni_strdup( props.pal_lz ? "-pzl" : "-pz!" );
		uni_arr_app( flags, &temp );
	}

	if( props.pal )
	{
		temp = uni_strdup( "-p" );
		uni_arr_app( flags, &temp );

		if( props.palsz > 0 )
		{
			u32 palsz = props.palsz + 1;
			char* tmp = uni_alloc( sizeof( char ) * 7 );
			uni_memcpy( tmp, "-pn", 3 );

			if( palsz < 100 )
			{
				if( palsz < 10 )
				{
					tmp[4] = '\0';
					tmp[3] = (char)( palsz + 0x30 );
				}
				else
				{
					tmp[5] = '\0';
					tmp[4] = (char)( ( palsz % 10 ) +
						0x30 );
					tmp[3] = (char)( ( palsz / 10 ) +
						0x30 );
				}
			}
			else
			{
				/* decimal is annoying */
				tmp[6] = '\0';
				tmp[5] = (char)( ( palsz % 10 ) + 0x30 );
				tmp[4] = (char)( ( palsz % 100 ) -
					( palsz / 10 ) + 0x30 );
				tmp[3] = (char)( palsz - ( palsz / 100 ) +
					0x30 );
			}

			temp = uni_strdup( tmp );
			uni_arr_app( flags, &temp );
			uni_free( tmp );
		}
		else
		{
			temp = uni_strdup( props.bpp == BPP_8
					? "-pn256"
					: props.bpp == BPP_4 ? "-pn16"
							     : "-pn2" );
			uni_arr_app( flags, &temp );
		}
	}
	else
	{
		temp = uni_strdup( "-p!" );
		uni_arr_app( flags, &temp );
	}

	temp = uni_strdup( "-ftb" );
	uni_arr_app( flags, &temp );
	temp = uni_strdup( "-fh!" );
	uni_arr_app( flags, &temp );

	{
		struct uni_str* str;
		const char* made;

		str = uni_str_init( oname );
		uni_str_prep( str, "-o" );
		made = uni_str_make( str );
		uni_str_fini( str );
		/* this alloc now belongs to flags */
		uni_arr_app( flags, &made );
	}

	temp = uni_strdup( iname );
	uni_arr_prep( flags, &temp );

	temp = uni_strdup( "grit" );
	uni_arr_prep( flags, &temp );

	temp = NULL;
	uni_arr_app( flags, &temp );

	{
		char** ret;

		ret = uni_arr_make( flags );
		uni_arr_fini( flags );

		return ret;
	}
}

const char* bin2asmcall(
	const char* file, const char* sym, const char* midsuf )
{
	struct uni_str* str;
	struct uni_arr* arr;
	const char* temp;
	const char* ret;
	char* const* args;

	arr = uni_arr_init( sizeof( char* ) );

	temp = uni_strdup( "bin2asm" );
	uni_arr_app( arr, &temp );

	str = uni_str_init( file );
	uni_str_app( str, midsuf );
	uni_str_app( str, ".bin" );

	temp = uni_str_make( str );
	uni_arr_app( arr, &temp );
	uni_str_fini( str );

	str = uni_str_init( file );
	uni_str_app( str, midsuf );
	uni_str_app( str, ".s" );

	temp = uni_str_make( str );
	ret  = uni_strdup( temp );
	uni_arr_app( arr, &temp );
	uni_str_fini( str );

	temp = uni_strdup( "-s" );
	uni_arr_app( arr, &temp );

	temp = uni_strdup( sym );
	uni_arr_app( arr, &temp );

	temp = NULL;
	uni_arr_app( arr, &temp );

	args = uni_arr_make( arr );
	uni_arr_fini( arr );

	excall( "bin2asm", args );

	uni_strfreev( (char**)args );

	return ret;
}

#define ENSURE( _cnd, _msg ) \
	do \
	{ \
		if( _cnd ) \
		{ \
		} \
		else \
		{ \
			fprintf( stderr, "%s\n", ( _msg ) ); \
			return 127; \
		} \
	} while( 0 )

int main( int ac, const char* av[] )
{
	const char* iname;
	const char* oname;
	const char* tmpname;
	const char* temp;
	struct gfxprops props;
	const char* const cwd = getcwd( NULL, 0 );

	ENSURE( cwd != NULL, "Cannot get current working directory" );

	if( ac <= 1 ||
		( ac == 2 &&
			( uni_strequ( av[1], "--help" ) ||
				uni_strequ( av[1], "-h" ) ) ) )
	{
		printf( "%s%s", helptxt, helptxt2 );

		return 0;
	}

	ENSURE( !uni_strequ( av[1], "-" ), "Cannot read from standard input" );
	ENSURE( uni_strsuf( av[1], ".png" ), "Input must be a PNG image" );

	/* resolve the symbolic path from given input */
	if( uni_strstr( av[1], cwd ) == NULL )
	{
		char** tmp;

		ENSURE( uni_strpre( av[1], "data/" ),
			"Current working directory is not present in path and the path given\ndoes not start with 'data/' (cf. ADP 1). Cannot deduce the\nsymbol name." );

		tmp = uni_strsplit( av[1], "data/", 2 );
		ENSURE( uni_strlenv( tmp ) == 2, "Invalid input file path" );
		iname = uni_strdup( tmp[1] );
		uni_strfreev( tmp );
	}
	else
	{
		char** tmp;

		tmp = uni_strsplit( av[1], cwd, 2 );
		ENSURE( uni_strlenv( tmp ) == 2, "Invalid input file path" );
		iname = uni_strdup( tmp[1] );
		uni_strfreev( tmp );

		if( uni_strpre( iname, "data/" ) )
		{
			tmp = uni_strsplit( iname, "data/", 2 );
			uni_free( (char*)iname );
			ENSURE( uni_strlenv( tmp ) == 2,
				"Invalid input file path" );
			iname = uni_strdup( tmp[1] );
			uni_strfreev( tmp );
		}
	}

	if( ac < 3 )
	{
		char** tmp;
		struct uni_str* tmpstr;

		tmp    = uni_strsplit( av[1], ".png", 2 );
		tmpstr = uni_str_init( tmp[0] );
		uni_str_app( tmpstr, ".o" );
		oname = uni_str_make( tmpstr );
		uni_str_fini( tmpstr );
		uni_strfreev( tmp );
	}
	else if( ac == 3 )
	{
		oname = uni_strdup( av[2] );
	}
	else
	{
		fprintf( stderr, "Too many arguments provided: %u\n", ac );

		return 127;
	}

	/* resolve the output props */
	{
		int r;

		r = parse_ext( iname, &props );

#define EXT_ERR( _msg ) \
	do \
	{ \
		fprintf( stderr, "Bad file extension: %s\n", ( _msg ) ); \
		return 127; \
	} while( 0 )

		switch( r )
		{
		case 0:
			break;
		case ERR_PARSE_EXT_TOO_FEW_DOTS:
			EXT_ERR( "Too few dots for metadata" );
		case ERR_PARSE_EXT_INVALID_OUT_TYPES:
			EXT_ERR( "invalid output types" );
		case ERR_PARSE_EXT_INVALID_GFX_FORM:
			EXT_ERR( "ill-defined graphics form" );
		case ERR_PARSE_EXT_INVALID_BPP:
			EXT_ERR( "bad BPP (must be 1, 4 or 8)" );
		default:
			EXT_ERR( "unknown error" );
		}
#undef EXT_ERR
	}

	/* make the grit flags and execute the call */
	{
		char* const* gritopts;
		struct uni_str* tmpstr;
		const char* temp2;

		tmpname = uni_strdup( tmpnam( NULL ) );

		tmpstr = uni_str_init( tmpname );
		uni_str_app( tmpstr, ".bin" );
		temp2 = uni_str_make( tmpstr );
		uni_str_fini( tmpstr );

		gritopts = mkgritflags( props, av[1], temp2 );
		uni_free( (char*)temp2 );
		excall( "grit", gritopts );
	}

	/* mangle the symbols and convert the binary file to assembly */
	{
		const char* temp2;
		const char** temparr;
		struct uni_arr* sfiles;

		temparr = (const char**)uni_strsplit( iname, ".", -1 );
		temp2   = (const char*)uni_strdup( temparr[0] );
		uni_strfreev( (char**)temparr );
		temparr = (const char**)uni_strsplit( temp2, "/", -1 );
		uni_free( (char*)temp2 );
		sfiles = uni_arr_init( sizeof( char* ) );

		if( props.img )
		{
			eg_mangle( temparr,
				props.img_lz ? "imgl" : "img",
				&temp );
			temp2 = bin2asmcall( tmpname, temp, ".img" );
			uni_arr_app( sfiles, &temp2 );
			uni_free( (char*)temp );
		}

		if( props.map )
		{
			eg_mangle( temparr,
				props.map_lz ? "mapl" : "map",
				&temp );
			temp2 = bin2asmcall( tmpname, temp, ".map" );
			uni_arr_app( sfiles, &temp2 );
			uni_free( (char*)temp );
		}

		if( props.pal )
		{
			eg_mangle( temparr,
				props.pal_lz ? "pall" : "pal",
				&temp );
			temp2 = bin2asmcall( tmpname, temp, ".pal" );
			uni_arr_app( sfiles, &temp2 );
			uni_free( (char*)temp );
		}

		temp2 = NULL;
		uni_arr_app( sfiles, &temp2 );

		uni_strfreev( (char**)temparr );
		temparr = uni_arr_make( sfiles );
		uni_arr_fini( sfiles );

		/* call the assembler */
		{
			struct uni_arr* args;
			char* const* argv;
			ptri i;

			args  = uni_arr_init( sizeof( char* ) );
			temp2 = uni_strdup( "arm-none-eabi-as" );
			uni_arr_app( args, &temp2 );
			temp2 = uni_strdup( "-mcpu=arm7tdmi" );
			uni_arr_app( args, &temp2 );
			temp2 = uni_strdup( "-march=armv4t" );
			uni_arr_app( args, &temp2 );
			temp2 = uni_strdup( "-o" );
			uni_arr_app( args, &temp2 );
			temp2 = uni_strdup( oname );
			uni_arr_app( args, &temp2 );

			for( i = 0; temparr[i] != NULL; ++i )
			{
				uni_arr_app( args, (char**)( &temparr[i] ) );
			}

			temp2 = NULL;
			uni_arr_app( args, &temp2 );

			argv = uni_arr_make( args );
			uni_arr_fini( args );

			excall( "arm-none-eabi-as", argv );

			uni_strfreev( (char**)argv );
		}
	}

	/* TODO: Remove other temporary files */

	return 0;
}
