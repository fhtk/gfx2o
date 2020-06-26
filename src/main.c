/* -*- coding: utf-8 -*- */
/****************************************************************************\
 *                                  gfx2o™                                  *
 *                                                                          *
 *                         Copyright © 2020 Aquefir                         *
 *                           Released under MPL2.                           *
\****************************************************************************/

#include <glib.h>
#include <stdio.h>

static const char* helptxt = "PNG graphics to object code converter\n\nUsage:\n    \n    gfx2obj.py <input> [output]\n    Takes a PNG file <input>, runs it through grit, tags it with\n    the necessary symbols, and outputs an object code file.\n\ngfx2obj takes all of its metadata hints from the file\nextension provided. It uses this format (regex):\n\n    \\.[148](tn?|b)\\.(il?)?(ml?)?(pl?([0-9]{1,3})?)?\\.png$\n\nThe bpp portion specifies its bits-per-pixel, 4 or 8.\nThe next portion specifies what form the image takes on the GBA:\ntile or bitmap based. If \"tn\" is used, no tile reduction is done.\nThe next part specifies what kind of outputs to emit (i for\nimage/tileset, m for tilemap, and p for palette), and whether to\ncompress each output (l suffix, using LZ77). The optional numeric\nspecifies exactly how many colours the palette should have, instead\nof the maximum for the given bit depth.\n";

enum
{
	BPP_1 = 0,
	BPP_2,
	BPP_4,
	BPP_8
};

struct gfxprops
{
	guint8 img : 1;
	guint8 img_lz : 1;
	guint8 map : 1;
	guint8 map_lz : 1;
	guint8 pal : 1;
	guint8 pal_lz : 1;
	guint8 reduce : 1;
	guint8 tile : 1;
	guint8 palsz;
	guint8 bpp : 2;
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
		: tmpch == '2' ? BPP_2
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
}

int main( int ac, char* av[] )
{
	gchar** tmpstrv;
	gchar* name;
	gchar* tmpstr;
	guint tmpsz, i;

	if( ac <= 1 || (ac == 2 && (!strcmp( av[1], "--help") || !strcmp( av[1], "-h"))))
	{
		printf( helptxt );

		return 0;
	}

	if( !strcmp( av[1], "-" ))
	{
		fprintf( stderr, "Cannot read from standard input!\n" );

		return 2;
	}

	/* split off leading directories */
	tmpstrv = g_strsplit( av[1], "data/", -1 );
	tmpsz = g_strv_length( tmpstrv );
	/* dup and save last part after “data/” */
	tmpstr = g_strdup( tmpstrv[tmpsz - 1] );
	g_strfreev( tmpstrv );

	/* split after first dot to get symbolic name */
	tmpstrv = g_strsplit( tmpstr, ".", 2 );
	tmpsz = g_strv_length( tmpstrv );
	g_free( tmpstr );

	if(tmpsz == 0)
	{
		fprintf( stderr, "No file extension for file!\n" );
		g_strfreev( tmpstrv );

		return 3;
	}

	tmpstr = g_strdup( tmpstrv[1] );
	g_strfreev( tmpstrv );
	tmpsz = strlen( tmpstr );

	/* replace slashes with underscores for symbolic name */
	for(i = 0; i < tmpsz; ++i)
	{
		if(tmpstr[i] == '/')
		{
			tmpstr[i] = '_';
		}
	}

	return 0;
}
