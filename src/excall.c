/* -*- coding: utf-8 -*- */
/****************************************************************************\
 *                                  gfx2o™                                  *
 *                                                                          *
 *                         Copyright © 2020 Aquefir                         *
 *                           Released under MPL2.                           *
\****************************************************************************/

#include <unistd.h>

int excall( const char* nam, char* const* av )
{
	int pid;

	pid = fork( );

	if( pid == -1 )
	{
		return 1;
	}
	else if( pid == 0 )
	{
		execvp( nam, av );
	}

	return 0;
}
