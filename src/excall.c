/* -*- coding: utf-8 -*- */
/****************************************************************************\
 *                                  gfx2o™                                  *
 *                                                                          *
 *                         Copyright © 2020 Aquefir                         *
 *                           Released under MPL2.                           *
\****************************************************************************/

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

int excall( const char* nam, char* const* av )
{
	pid_t pid;

	pid = fork( );

	if( pid == -1 )
	{
		return 1;
	}
	else if( pid == 0 )
	{
		execvp( nam, av );
	}
	else if( pid > 0 )
	{
		int status;
		pid_t r;

		r = waitpid( pid, &status, 0 );

		if( r < 0 )
		{
			return 1;
		}
		else if( r == pid && !WIFEXITED( status ) )
		{
			return 1;
		}
	}

	return 0;
}
