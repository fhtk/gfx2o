/* -*- coding: utf-8 -*- */
/****************************************************************************\
 *                                  gfx2o™                                  *
 *                                                                          *
 *                         Copyright © 2020 Aquefir                         *
 *                           Released under MPL2.                           *
\****************************************************************************/

#include <glib.h>
#include <stdio.h>
#include <unistd.h>

static const char* helptxt = "PNG graphics to object code converter\n\nUsage:\n    \n    gfx2obj <input> [output]\n    Takes a PNG file <input>, runs it through grit, tags it with\n    the necessary symbols, and outputs an object code file.\n\ngfx2obj takes all of its metadata hints from the file\nextension provided. It uses this format (regex):\n\n    \\.[148](tn?|b)\\.(il?)?(ml?)?(pl?([0-9]{1,3})?)?\\.png$\n\nThe bpp portion specifies its bits-per-pixel, 4 or 8.\nThe next portion specifies what form the image takes on the GBA:\n";
static const char* helptxt2 = "tile or bitmap based. If \"tn\" is used, no tile reduction is done.\nThe next part specifies what kind of outputs to emit (i for\nimage/tileset, m for tilemap, and p for palette), and whether to\ncompress each output (l suffix, using LZ77). The optional numeric\nspecifies exactly how many colours the palette should have, instead\nof the maximum for the given bit depth.\n";

enum
{
	BPP_1 = 0,
	BPP_4,
	BPP_8
};

struct gfxprops
{
	guint img : 1;
	guint img_lz : 1;
	guint map : 1;
	guint map_lz : 1;
	guint pal : 1;
	guint pal_lz : 1;
	guint reduce : 1;
	guint tile : 1;
	guint palsz;
	guint bpp : 2;
};

static gboolean parse_ext( const gchar* fname, struct gfxprops* o, GError** e )
{
	struct gfxprops out;
	gchar** spl, tmpch;
	guint spl_sz, tmpsz, tmpsz_wpal, i, fac;
	GError* err;

	spl = g_strsplit( fname, ".", -1 );
	spl_sz = g_strv_length( spl );

	if(spl_sz < 3)
	{
		err = g_error_new( 6900, 1,
			"Parsing of gfx file extension metadata failed" );

		e = &err;

		return TRUE;
	}

	tmpsz = strlen( spl[spl_sz - 2] );
	/* this is saved so we know how long the string with palette count is */
	tmpsz_wpal = tmpsz;

	/* reduce until no more numbers are left on the end
	 * these numbers, if present, explicitly specify palette count */
	for(tmpsz_wpal = tmpsz; spl[spl_sz - 2][tmpsz - 1] >= 0x30
	|| spl[spl_sz - 2][tmpsz - 1] <= 0x39; --tmpsz);

	/* parse the output selector chars */
	for(i = 0; i < tmpsz; ++i)
	{
		gchar doneletter = spl[spl_sz - 2][i];

		switch(spl[spl_sz - 2][i])
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
			err = g_error_new( 6900, 2,
				"Invalid output selector: '%c'", spl[spl_sz - 2][i] );
			e = &err;

			return TRUE;
		case 'l':
			doneletter = '\0';
			/* this is handled outside this switchcase */
			break;
		}

		if(doneletter && i < tmpsz - 1 && spl[spl_sz - 2][i + 1])
		{
			switch(doneletter)
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

	fac = 1;
	out.palsz = 0;

	/* parse the palette size, if present */
	for(i = tmpsz_wpal - 1; i >= tmpsz; --i)
	{
		out.palsz += (spl[spl_sz - 2][i] - 0x30) * fac;

		fac *= 10;
	}

	/* if there was no explicit palette size, default it */
	if(out.palsz == 0)
	{
		out.palsz = 255;
	}

	/* check size first */
	tmpsz = strlen( spl[spl_sz - 3] );

	if( tmpsz < 2 )
	{
		err = g_error_new( 6900, 3, "Invalid form field '%s'", spl[spl_sz - 3] );
		e = &err;

		return TRUE;
	}

	/* get BPP */
	tmpch = spl[spl_sz - 3][0];

	out.bpp = tmpch == '1' ? BPP_1
		: tmpch == '4' ? BPP_4
		: BPP_8;

	if(out.bpp == BPP_8 && tmpch != '8')
	{
		err = g_error_new( 6900, 4, "Invalid image BPP: '%c'", tmpch );
		e = &err;

		return TRUE;
	}

	/* get form (bitmap or tile) */
	switch(spl[spl_sz - 3][1])
	{
	case 'b':
		out.tile = 0;
		break;
	case 't':
		out.tile = 1;
		break;
	default:
		err = g_error_new( 6900, 4, "Invalid image form: '%c'",
			spl[spl_sz - 3][1] );
		e = &err;

		return TRUE;
	}

	out.reduce = 1;

	if(out.tile && tmpsz >= 3 && spl[spl_sz - 3][2] == 'n')
	{
		out.reduce = 0;
	}

	return FALSE;
}

static const char** mk_gritflags( struct gfxprops props )
{
	char* o[16];
	/* this is sometimes used to temp alloc a certain string below */
	char* astr;
	char** out;
	size_t i, j;

	astr = NULL;
	i = 0;

	if(props.img)
	{
		o[i++] = "-g";

		if(props.img_lz)
		{
			o[i++] = "-gzl";
		}
		else
		{
			o[i++] = "-gz!";
		}
	}
	else
	{
		o[i++] = "-g!";
	}

	if(props.tile)
	{
		o[i++] = "-gt";
	}
	else
	{
		o[i++] = "-gb";
	}

	o[i++] = props.bpp == BPP_1 ? "-gB1"
		: props.bpp == BPP_4 ? "-gB4" : "-gB8";

	if(props.map)
	{
		o[i++] = "-m";

		if(props.map_lz)
		{
			o[i++] = "-mzl";
		}
		else
		{
			o[i++] = "-mz!";
		}
	}
	else
	{
		o[i++] = "-m!";
	}

	if(props.reduce)
	{
		o[i++] = "-mRtf";
	}
	else
	{
		o[i++] = "-mR!";
	}

	if(props.pal)
	{
		o[i++] = "-p";

		if(props.pal_lz)
		{
			o[i++] = "-pzl";
		}
		else
		{
			o[i++] = "-pz!";
		}

		if(props.palsz > 0)
		{
			astr = g_malloc( sizeof(char) * 7 );
			memcpy( astr, "-pn", 3 );

			if(props.palsz < 100)
			{
				if(props.palsz < 10)
				{
					astr[4] = '\0';
					astr[3] = (char)(props.palsz + 0x30);
				}
				else
				{
					astr[5] = '\0';
					astr[4] = (char)((props.palsz % 10) + 0x30);
					astr[3] = (char)(props.palsz - (props.palsz % 10) + 0x30);
				}
			}
			else
			{
				/* decimal is annoying */
				astr[6] = '\0';
				astr[5] = (char)((props.palsz % 10) + 0x30);
				astr[4] = (char)((props.palsz % 100) - (props.palsz % 10) + 0x30);
				astr[3] = (char)(props.palsz - (props.palsz % 100)
					- (props.palsz % 10) + 0x30);
			}

			/* store the pointer for consumption */
			o[i++] = astr;
		}
		else if(props.bpp == BPP_8)
		{
			o[i++] = "-pn256";
		}
		else if(props.bpp == BPP_4)
		{
			o[i++] = "-pn16";
		}
		else if(props.bpp == BPP_1)
		{
			o[i++] = "-pn2";
		}
	}
	else
	{
		o[i++] = "-p!";
	}

	/* a new allocation for the output
	 *  1. all strings above are either static const or temp alloc’d
	 *  2. the char* array is stack allocated too
	 */
	out = g_malloc( sizeof( char * ) * (i + 1) );

	for(j = 0; j < i; ++j)
	{
		size_t len;

		len = strlen( o[j] );
		out[j] = g_malloc( sizeof(char) * (len + 1) );
		memcpy( out[j], o[j], len );
		out[j][len] = '\0';
	}

	/* NULL-terminated char* array */
	out[i] = NULL;

	if(astr != NULL)
	{
		/* deallocate this since it has been consumed for out */
		g_free( astr );
		astr = NULL;
	}

	return (const char**)out;
}

int main( int ac, char* av[] )
{
	gchar** tmpstrv;
	gchar* name;
	gchar* tmpstr;
	gchar* tempfile;
	gchar* cwd;
	guint tmpsz, i;

	if( ac <= 1 || (ac == 2 && (!strcmp( av[1], "--help") || !strcmp( av[1], "-h"))))
	{
		printf( helptxt );
		printf( helptxt2 );

		return 0;
	}

	if( !strcmp( av[1], "-" ))
	{
		fprintf( stderr, "Cannot read from standard input!\n" );

		return 2;
	}

	cwd = getcwd( NULL, 0 );

	if(cwd == NULL)
	{
		fprintf( stderr, "Cannot get current working directory.\n" );

		return 2;
	}

	if( g_strrstr( av[1], (const gchar*)cwd ) == NULL )
	{
		if( !g_str_has_prefix( av[1], "data/" ) )
		{
			fprintf( stderr, "Current working directory is not present in path and the path given\ndoes not start with 'data/' (cf. slick/fsschema). Cannot deduce the\nsymbol name. Exiting...\n" );

			return 2;
		}

		tmpstrv = g_strsplit( av[1], "data/", 2 );
		tmpstr = g_strdup( tmpstrv[1] );
		g_strfreev( tmpstrv );
	}
	else
	{
		tmpstrv = g_strsplit( av[1], (const gchar*)cwd, 2 );
		tmpstr = g_strdup( tmpstrv[1] );
		g_strfreev( tmpstrv );

		if( g_str_has_prefix( (const gchar*)tmpstr, "data/" ) )
		{
			tmpstrv = g_strsplit( (const gchar*)tmpstr, "data/", 2 );
			g_free( tmpstr );
			tmpstr = g_strdup( tmpstrv[1] );
			g_strfreev( tmpstrv );
		}
	}

	tmpstrv = g_strsplit( (const gchar*)tmpstr, "/", -1 );
	tmpsz = g_strv_length( tmpstrv );
	g_free( tmpstr );

	if( !g_str_has_suffix( (const gchar*)(tmpstrv[tmpsz - 1]), ".png" ) )
	{
		fprintf( stderr, "The file passed is not a PNG image.\n" );
		g_strfreev( tmpstrv );

		return 2;
	}

	/* TODO: transform the .png into the desired type suffix for mangledeggs */

	g_strfreev( tmpstrv );

	return 0;
}
