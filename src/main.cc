/*
 *  Stage : a multi-robot simulator.
 *  Copyright (C) 2001, 2002 Richard Vaughan, Andrew Howard and Brian Gerkey.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
/*
 * Desc: Program Entry point
 * Author: Andrew Howard, Richard Vaughan
 * Date: 12 Mar 2001
 * CVS: $Id: main.cc,v 1.56 2002-10-27 21:55:37 rtv Exp $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h> // for gethostbyname(3)

#include "server.hh"
#include "library.hh"

// defined in library.cc
extern Library model_library; 

//#define DEBUG


///////////////////////////////////////////////////////////////////////////
// Global vars

// Quit signal
bool quit = false;

// Pointer to the one-and-only instance of the world
// This really should be static
static CWorld *world = NULL;


///////////////////////////////////////////////////////////////////////////
// Print the usage string
void PrintUsage( void )
{
  printf("\nUsage: stage [options] <worldfile>\n"
	 "Options: <argument> [default]\n"
	 " -p <portnum>\tSet the server port [6601]\n"
	 " -c <hostname>\tRun as a client to a Stage server on hostname\n"
	 " -cl\t\tRun as a client to a Stage server on localhost\n"
	 " -g\t\tDo not start the X11 GUI\n"
	 " -n \t\tDo not start Player\n"
	 " -o\t\tEnable console status output\n"
	 " -v <float>\tSet the simulated time increment per cycle [0.1sec].\n"
	 " -u <float>\tSet the desired real time per cycle [0.1 sec].\n"
	 " -f \t\tRun as fast as possible; don't try to match real time\n"
	 " -r <IP:port>\tSend sensor data to this address in RTP format\n"
	 " -l <filename>\tLog some timing and throughput statistics into <filename>.<incremental suffix>\n"
	 //" -r <IP:port>\tSend sensor data to this address in RTP format\n"
	 //#ifdef HRL_HEADERS
	 //" -i\t\tSend IDAR messages to XS (hrlstage only)\n"
	 //#endif
	 "Command-line options override any configuration file equivalents.\n"
	 "\n"
	 "Richard Vaughan, Andrew Howard, Brian Gerkey and contributors 2000-2002\n"
	 "Released under the GNU General Public License.\n"
	 "\n"
	 );
}

///////////////////////////////////////////////////////////////////////////
// Clean up and quit
void StageQuit( void )
{  
  puts( "\n** Stage quitting **" );
  
  if( world )  
  {
    world->Shutdown();  // Stop the world
    delete world;       // Destroy the world
  }
  exit( 0 );
}

///////////////////////////////////////////////////////////////////////////
// Handle quit signals
void sig_quit(int signum)
{
  PRINT_DEBUG1( "SIGNAL %d\n", signum );
  quit = true;
}


///////////////////////////////////////////////////////////////////////////
// Program entry
//
int main(int argc, char **argv)
{  
  // hello world
  printf("\n** Stage  v%s ** ", (char*) VERSION);

  fflush( stdout );
 
  world = NULL;
  
  // CStageServer and CStageClient are subclasses of CStageIO and CWorld
  // check the command line for the '-c' option that makes this a client
  for( int a=1; a<argc; a++ )
  {
    PRINT_DEBUG2( "argv[%d] = %s\n", a, argv[a] );
      
    if( strcmp( argv[a], "-c" ) == 0 ||  strcmp( argv[a], "-cl" ) == 0)
      assert( world = new CStageClient( argc, argv, &model_library ) );
  }
  
  // if we're not a client, we must be a server
  if( world == NULL )
    assert( world = new CStageServer( argc, argv, &model_library ) );
  
  // a world constructor may have raised the quit flag
  // (this would be more elegantly implemented with an exception..)
  if( quit )
  {
    puts( "Stage: failed to create a world. Quitting." );
    exit( 0 );
  }

  // load the world model.
  //
  //  a server loads this from a world file; a client downloads it
  // from a server

  if( !world->Load() )
  {
    puts("Stage: failed to load world. Quitting.");
    StageQuit();
  }

  // startup is (externally) identical for client and server, but they
  // do slightly different things inside.
  // post-constructor startup is required to exploit object polymorphism
  if (!world->Startup())
  {
    puts("Stage: failed to startup world. Quitting.");
    StageQuit();
  }
  
  puts( "" ); // end the startup output line
  
  // Register callback for quit (^C,^\) events
  // - sig_quit function raises the quit flag 
  signal(SIGINT, sig_quit );
  signal(SIGQUIT, sig_quit );
  signal(SIGTERM, sig_quit );
  signal(SIGHUP, sig_quit );
  
  // the main loop
    
  // update the simulation - stop when the quit flag is raised
  while( !quit ) world->Update(); 
  
  //gtk_main();

  // clean up and exit
  StageQuit();
}


