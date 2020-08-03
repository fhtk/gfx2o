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
	u32 spl_sz, tmpsz, tmpsz_wpal, i, fac;

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
	for( tmpsz_wpal = tmpsz; spl[spl_sz - 2][tmpsz - 1] >= 0x30 ||
	     spl[spl_sz - 2][tmpsz - 1] <= 0x39;
	     --tmpsz )
		;

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

	fac       = 1;
	out.palsz = 0;

	/* parse the palette size, if present */
	for( i = tmpsz_wpal - 1; i >= tmpsz; --i )
	{
		out.palsz += ( spl[spl_sz - 2][i] - 0x30 ) * fac;

		fac *= 10;
	}

	/* if there was no explicit palette size, default it */
	if( out.palsz == 0 )
	{
		out.palsz = 255;
	}

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

static char** mkgritflags( struct gfxprops props, const char* oname )
{
	struct uni_arr* flags;

	flags = uni_arr_init( 0 );

	if( props.img )
	{
		uni_arr_app( flags, uni_strdup( "-g" ) );
		uni_arr_app(
		   flags, uni_strdup( props.img_lz ? "-gzl" : "-gz!" ) );
		uni_arr_app( flags, uni_strdup( props.tile ? "-gt" : "-gb" ) );
		uni_arr_app( flags,
		   uni_strdup( props.bpp == BPP_1
		         ? "-gB1"
		         : props.bpp == BPP_4 ? "-gB4" : "-gB8" ) );
	}
	else
	{
		uni_arr_app( flags, uni_strdup( "-g!" ) );
	}

	if( props.map )
	{
		uni_arr_app( flags, uni_strdup( "-m" ) );
		uni_arr_app(
		   flags, uni_strdup( props.map_lz ? "-mzl" : "-mz!" ) );
		uni_arr_app(
		   flags, uni_strdup( props.reduce ? "-mRtf" : "-mR!" ) );
	}
	else
	{
		uni_arr_app( flags, uni_strdup( "-m!" ) );
		uni_arr_app(
		   flags, uni_strdup( props.pal_lz ? "-pzl" : "-pz!" ) );
	}

	if( props.pal )
	{
		uni_arr_app( flags, uni_strdup( "-p" ) );

		if( props.palsz > 0 )
		{
			char* tmp = uni_alloc( sizeof( char ) * 7 );
			uni_memcpy( tmp, "-pn", 3 );

			if( props.palsz < 100 )
			{
				if( props.palsz < 10 )
				{
					tmp[4] = '\0';
					tmp[3] = (char)( props.palsz + 0x30 );
				}
				else
				{
					tmp[5] = '\0';
					tmp[4] = (char)( ( props.palsz % 10 ) +
					   0x30 );
					tmp[3] = (char)( props.palsz -
					   ( props.palsz % 10 ) + 0x30 );
				}
			}
			else
			{
				/* decimal is annoying */
				tmp[6] = '\0';
				tmp[5] = (char)( ( props.palsz % 10 ) + 0x30 );
				tmp[4] = (char)( ( props.palsz % 100 ) -
				   ( props.palsz % 10 ) + 0x30 );
				tmp[3] = (char)( props.palsz -
				   ( props.palsz % 100 ) -
				   ( props.palsz % 10 ) + 0x30 );
			}

			uni_arr_app( flags, uni_strdup( tmp ) );
			uni_free( tmp );
		}
		else
		{
			uni_arr_app( flags,
			   uni_strdup( props.bpp == BPP_8
			         ? "-pn256"
			         : props.bpp == BPP_4 ? "-pn16" : "-pn2" ) );
		}
	}
	else
	{
		uni_arr_app( flags, uni_strdup( "-p!" ) );
	}

	uni_arr_app( flags, uni_strdup( "-ftb" ) );
	uni_arr_app( flags, uni_strdup( "-fh!" ) );

	{
		struct uni_str* str;
		const char* made;

		str = uni_str_init( oname );
		uni_str_app( str, ".bin" );
		made = uni_str_make( str );
		uni_str_fini( str );
		/* this alloc now belongs to flags */
		uni_arr_app( flags, made );
	}

	uni_arr_prep( flags, uni_strdup( "grit" ) );

	{
		char** ret;

		ret = uni_arr_make( flags );
		uni_arr_fini( flags );

		return ret;
	}
}

int main( int ac, const char* av[] )
{
	char** tmpstrv;
	char** gritflags;
	char* name;
	char* symbol;
	char* tmpstr;
	char* tmpstr2;
	char* tmpfpath;
	struct gfxprops props;
	u32 tmpsz, i;
	int pid;
	struct uni_string* name_in;
	ptri name_insz;
	const char* cwd = getcwd( NULL, 0 );

	if( cwd == NULL )
	{
		fprintf( stderr, "Cannot get current working directory.\n" );

		return 2;
	}

	if( ac <= 1 ||
	   ( ac == 2 &&
	      ( uni_strequ( av[1], "--help" ) ||
	         uni_strequ( av[1], "-h" ) ) ) )
	{
		printf( helptxt );
		printf( helptxt2 );

		return 0;
	}

	if( uni_strequ( av[1], "-" ) )
	{
		fprintf( stderr, "Cannot read from standard input!\n" );

		return 2;
	}

	if( !uni_strsuf( av[1], ".png" ) )
	{
		fprintf( stderr, "The file passed is not a PNG image.\n" );

		return 2;
	}

	if( uni_strrstr( av[1], (const char*)cwd ) == NULL )
	{
		if( !uni_strpre( av[1], "data/" ) )
		{
			fprintf( stderr,
			   "Current working directory is not present in path and the path given\ndoes not start with 'data/' (cf. slick/fsschema). Cannot deduce the\nsymbol name. Exiting...\n" );

			return 2;
		}

		tmpstrv = uni_strsplit( av[1], "data/", 2 );
		name    = uni_strdup( tmpstrv[1] );
		uni_strfreev( tmpstrv );
	}
	else
	{
		tmpstrv = uni_strsplit( av[1], (const char*)cwd, 2 );
		name    = uni_strdup( tmpstrv[1] );
		uni_strfreev( tmpstrv );

		if( uni_strpre( (const char*)name, "data/" ) )
		{
			tmpstrv =
			   uni_strsplit( (const char*)name, "data/", 2 );
			uni_free( name );
			name = uni_strdup( tmpstrv[1] );
			uni_strfreev( tmpstrv );
		}
	}

	/* create the argv[] to pass to grit */
	gritflags = mk_gritflags( props, (const char*)name );

	/* TODO: transform the .png into the desired type suffix for
	 * mangledeggs */
	name_in   = uni_str_init( (const char*)name );
	name_insz = uni_str_getsz( name_in );

	if( excall( "grit", gritflags ) )
	{
		uni_strfreev( tmpstrv );

		return 2;
	}

	if( props.img )
	{
		struct rangep r  = { 0, name_insz - 4 };
		const char* nama = uni_str_mkslice( name_in, r );

		tmpstrv = uni_strsplit( nama, "/", 2 );
	}

	uni_strfreev( tmpstrv );

	return 0;
}
