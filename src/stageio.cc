#define DEBUG
#define VERBOSE

#include "server.hh"
#include "entity.hh"
#include "world.hh"
#include "fixedobstacle.hh"

#include <unistd.h>
#include <string.h>
#include <signal.h>

extern int g_timer_expired;

void CatchSigPipe( int signo ); // from server.cc

CStageIO::CStageIO( int argc, char** argv )
  : CWorld( argc, argv )
{
  m_port = DEFAULT_POSE_PORT;
  
  for( int a=1; a<argc-1; a++ )
  {
    // get the port number, overriding the default
    if( strcmp( argv[a], "-p" ) == 0 )
    {
      m_port = atoi(argv[a+1]);
      printf( "[Port %d]", m_port );
      a++;
    }
  }

  // catch signals generated by socket closures
  signal( SIGPIPE, CatchSigPipe );
 
  // init the pose server data structures
  m_pose_connection_count = 0;
  m_sync_counter = 0;

  // init the arrays
  memset( &m_pose_connections, 0, sizeof(struct pollfd)*MAX_POSE_CONNECTIONS); 
  memset( m_dirty_subscribe, 0, sizeof(char) * MAX_POSE_CONNECTIONS );
  memset( m_conn_type, 0, sizeof(char) * MAX_POSE_CONNECTIONS );
}  

CStageIO::~CStageIO( void )
{
  // close all connections
  for( int i=0; i<m_pose_connection_count; i++ )
    DestroyConnection( i );

//close( m_pose_connections[i].fd );
}

// Update() does the IO, and inherits the world's Update()
// hey, it's StageIO, right?
void CStageIO::Update( void )
{
  Read(); // synchronize world state
    
  CWorld::Update();

  Write(); // export world state
}

int CStageIO::WriteHeader( int fd, HeaderType type, uint32_t data )
{      
  stage_header_t hdr;
  
  hdr.type = type;
  hdr.data = data;
 
  //puts( "attempting to write a header" );

  int len = WritePacket( fd, (char*)&hdr, sizeof(hdr) );

  //printf( "wrote %d header bytes\n", len );

  return len;
}

int CStageIO::WriteCommand( int fd, cmd_t cmd )
{
  //puts( "attempting to write a command" );
  int len = WriteHeader( fd, StageCommand, cmd );
  //printf( "wrote %d command bytes\n", len );
  return len;
}

int CStageIO::WritePacket( int fd, char* data, size_t len )
{
  size_t writecnt = 0;
  int thiswritecnt;
  
  //printf( "attempting to write %d byte packet\n", len );

  while(writecnt < len )
  {
    thiswritecnt = write( fd, data+writecnt, len-writecnt);
      
    // check for error on write
    if( thiswritecnt == -1 )
      return -1; // fail
      
    writecnt += thiswritecnt;
  }

  //printf( "wrote %d/%d packet bytes\n", writecnt, len );

  return len; //success
}


int CStageIO::ReadPacket( int fd, char* buf, size_t len )
{
  //printf( "attempting to read a %d byte packet\n", len );

  assert( buf ); // data destination must be good

  int recv = 0;
  // read a header so we know what's coming
  while( recv < (int)len )
  {
    //printf( "Reading on %d\n", fd );
      
    /* read will block until it has some bytes to return */
    int r = read( fd, buf+recv,  len - recv );
      
    if( r == -1 ) // ERROR
    {
      if( errno != EINTR )
	    {
	      printf( "ReadPacket: read returned %d\n", r );
	      perror( "code" );
	      break;
	    }
    }
    else if( r == 0 ) // EOF
      break; 
    else
      recv += r;
  }      

  //printf( "read %d/%d bytes\n", recv, len );

  return recv; // the number of bytes read
}

int CStageIO::WriteProperty( int fd, stage_property_t* prop, 
			    char* data, size_t len )
{
  int res;
  
  assert( prop );
  assert( data );
  assert( len > 0 ); // no point sending a property with no data
  assert( prop->len == len ); // data size should match header len field
  assert( prop->property < MAX_NUM_PROPERTIES );

  //puts( "attempting to write a property" );
  
  res = WritePacket( fd, (char*)prop, sizeof(stage_property_t) );
  
  //printf( "write %d/%d property header bytes\n", res,sizeof(stage_property_t));
  //printf( "writing %d bytes of data\n", prop->len );
  
  res = WritePacket( fd, data, len );

  //printf( "wrote %d/%d bytes of property data\n", res, len );

  return 0;
}

int CStageIO::ReadProperty( int fd, stage_property_t* prop, 
			   char* data, size_t len )
{
  int res;

  assert( prop );
  assert( data );

  //puts( "attempting to read a property" );
  
  res = ReadPacket( fd, (char*)prop, sizeof(stage_property_t) );

  //printf( "read %d/%d property header bytes\n", res, sizeof(stage_property_t));
  //printf( "expecting %d bytes of data\n", prop->len );

  // validity checks 
  assert( prop->property < MAX_NUM_PROPERTIES );
  assert( prop->len > 0 );
  assert( prop->len < MAX_PROPERTY_DATA_LEN );
  assert( prop->len < len ); // must be enough space for the data

  // read data into the buffer
  res = ReadPacket( fd, data, prop->len );

  //printf( "read %d/%d bytes of property data\n", res, prop->len );

  return 0;
}

int CStageIO::ReadProperties( int con, int fd, int num )
{
  stage_property_t prop;
  char data[MAX_PROPERTY_DATA_LEN]; // XX define this somewhere
  
  for( int i=0; i<num; i++ )
  {
    ReadProperty( fd, &prop, data, MAX_PROPERTY_DATA_LEN );
      
    //printf( "attempting to set property %d of object %d"
    //    "with %d bytes of data starting at %p\n",
    //    prop.property, prop.id, prop.len, data );
      
    // set the property
    GetEntity( prop.id )->
      SetProperty( con, prop.property, (void*)data, prop.len );
  }
  
  return 0;
}


int CStageIO::ReadEntity( int fd, stage_entity_t* ent )
{
  assert( ent );
  
  int res = ReadPacket( fd, (char*)ent, sizeof(stage_entity_t) );
 
  return res;
}


int CStageIO::ReadEntities( int fd, int num )
{
  stage_entity_t ent;
  
  for( int i=0; i<num; i++ )
  {
    ReadEntity( fd, &ent );
     
    printf( "attempting to create entity %d:%d:%d\n",
            ent.id, ent.parent, ent.type );

    CEntity* obj = 0;
    if( ent.parent == -1 )
      assert( obj = CreateEntity( ent.type, 0 ) );
    else
      assert( obj = CreateEntity( ent.type, 
                                  GetEntity(ent.parent)));
		
    AddEntity( obj );
      
  }
  
  return 0;
}

int CStageIO::ReadBackground( int fd )
{
  if( this->wall )
    delete this->wall;
  
  stage_background_t w;
  
  int res = ReadPacket( fd, (char*)&w, sizeof(w) );
  
  if( res == sizeof(w) )
  {
    // Construct a single fixed obstacle representing
    // the environment.
    assert( this->wall = new CFixedObstacle(this, NULL ) );
      
    // poke the scale in from the data packet(ugly!)
    ((CFixedObstacle*)this->wall)->scale = w.scale;
      
    assert(this->matrix); // gotta have it before we can load the wall
      
    // read in the pixel array
    int num_pixels =  w.sizex * w.sizey;
    unsigned char* pixels = 0;
    assert( pixels = new unsigned char[ num_pixels ] );
      
    printf( "Attempting to read %d pixels into %p on %d\n",
            num_pixels, &pixels, fd );

    int res2 = ReadPacket( fd, (char*)pixels, num_pixels );
      
    if( res2 == num_pixels )
    {
      // create an image from the data
      assert( ((CFixedObstacle*)this->wall)->image = 
              new Nimage( pixels, w.sizex, w.sizey ) );      

      // wall->Startup is called at the end of the constructor
    }
    else
    {
      PRINT_ERR2( "short read (%d/%d pixels)\n", res2, num_pixels );
      return -1;
    }
  }
  else
    PRINT_ERR2( "short read (%d/%d bytes)\n", res, sizeof(w) );
  
  return res;
}

int CStageIO::WriteBackground( int fd )
{
  CFixedObstacle* fix = (CFixedObstacle*)this->wall;
  
  assert( fix );
  assert( fix->image );
  
  // announce that a matrix is on its way
  WriteHeader( fd, BackgroundPacket, 0);
  
  stage_background_t b;
  
  b.sizex = fix->image->get_width();
  b.sizey = fix->image->get_height();
  b.scale = fix->scale;
  //b.pixel_count = this->wall->image->count_pixels();

  printf( "Downloading background (%p)\n", &b ); 
  
  int res = WritePacket( fd, (char*)&b, sizeof(b) );
  
  // write the Nimage data as a big packet - this should be improved
  // to send only non-zero pixels to save bandwidth for large, sparse
  // images

  int num_pixels = b.sizex * b.sizey;
  int res2 = WritePacket( fd, 
			  (char*)fix->image->data, 
			  num_pixels );

  assert( res2 == num_pixels );

  return res;
}


int CStageIO::ReadMatrix( int fd )
{
  if( this->matrix )
    delete this->matrix;

  stage_matrix_t m;
  
  int res = ReadPacket( fd, (char*)&m, sizeof(m) );
 
  if( res == sizeof(m) )
    this->matrix = new CMatrix( m.sizex, m.sizey, 10 );
  else
    PRINT_ERR2( "short read (%d/%d bytes)", res, sizeof(m) );
 
  return res;
}

int CStageIO::WriteMatrix( int fd )
{
  assert( this->matrix );

  // announce that a matrix is on its way
  WriteHeader( fd,  MatrixPacket, 0);
 
  stage_matrix_t m;
  
  m.sizex = this->matrix->width;
  m.sizey = this->matrix->height;

  printf( "Downloading matrix (%p)\n", &m ); 
  
  int res = WritePacket( fd, (char*)&m, sizeof(m) );
  
  return res;
}

int CStageIO::WriteEntity( int fd, stage_entity_t* ent )
{
  assert( ent );
  
  printf( "Downloading entity %d.%d.%d\n", 
	  ent->id, ent->parent, ent->type ); 
  
  int res = WritePacket( fd, (char*)ent, sizeof(stage_entity_t) );
  
  return res;
}

int CStageIO::WriteEntities( int fd )
{
  int num_obs =  GetEntityCount();
  // announce that a load of entities are on their way
  WriteHeader( fd,  EntityPackets, num_obs );
  
  for( int n=0; n< num_obs; n++ )
  {
    CEntity* obj =  GetEntity(n);

    stage_entity_t ent;
    ent.id = n;
    ent.type = obj->stage_type;

    if( obj->m_parent_entity )
    {
      int m = 0;
      // figure out the parent's index
      while( GetEntity(m) != obj->m_parent_entity)
        m++;
	  
      if( m >= num_obs )
        puts( "Stage warning: parent index is out of range" );
	  
      ent.parent = m; // this is the parent's index
    }
    else
      ent.parent = -1; // no parent
      
    WriteEntity( fd, &ent );
  }

  return 0;
}


int CStageIO::ReadHeader( int fd, stage_header_t* hdr  )
{
  return ReadPacket( fd, (char*)hdr, sizeof(stage_header_t) );
}

//  int CStageIO::WriteSubscriptions( int fd )
//  #{
//  #  for( int i=0; i < GetEntityCount(); i++ )
//      if( GetEntity(i)->Subscribed() )
//        WriteHeader( fd, Subscribed, i );
  
//    return 0;
//  }


  /////////////////////////////////////////////////////////////
  // check for new and cancelled subscriptions
  // and inform clients of them


void CStageIO::Write( void )
{
  //PRINT_DEBUG( "STAGIO WRITE" );

  /////////////////////////////////////////////////////////
  // write out any properties that are dirty
  int i, p;
  
  char data[MAX_PROPERTY_DATA_LEN]; 

  // for all the connections
  for( int t=0; t< m_pose_connection_count; t++ )
    if( m_dirty_subscribe[t] ) // only send data to those who subscribed
    {
      int connfd = m_pose_connections[t].fd;
      
      assert( connfd > 0 ); // must be good
      
      int send_count =  CountDirtyOnConnection( t );

      if( send_count > 0 )
      {	    
        // announce the number of packets to follow on this connection
        //printf( "property packets header: "
        //    " %d packets, connection %d, fd %d\n",
        //    send_count, t, connfd );
	    
        WriteHeader( connfd,  PropertyPackets, send_count );
	    
        // loop through the entities again, this time sending the properties
        for( i=0; i < GetEntityCount(); i++ )
          for( p=0; p < MAX_NUM_PROPERTIES; p++ )
          {  
            //printf( "Inspecting entity %d property %d connection %d\n",
            //  i, p, t );
		  
            // is the entity marked dirty for this connection & prop?
            if( GetEntity(i)->m_dirty[t][p] )
            {
              //printf( "PROPERTY DIRTY dev: %d prop: %d\n", t, p);
		     
              int datalen = 
                GetEntity(i)->GetProperty((EntityProperty)p, data ); 
		      
              if( datalen == 0 )
              {
                PRINT_DEBUG1( "READ EMPTY PROPERTY %d\n",
                              p );
              }
              else
              {
                stage_property_t prop;
			  
                prop.id = i;
                prop.property = (EntityProperty)p; 
                prop.len = datalen;
			
  
                WriteProperty( connfd, &prop, data, datalen ); 
			  
              }
		      
              // mark it clean on this connection
              // it won't get re-sent here until this flag is set again
              GetEntity(i)->SetDirty( t, (EntityProperty)p, 0 );
            }
          }
      }
    }      
}

void CStageIO::DestroyConnection( int con )
{
#ifdef VERBOSE
  printf( "\nStage: Closing connection %d\n", con );
#endif

  close( m_pose_connections[con].fd );
  
  // if this was a sync connection, reduce the number of syncs we wait for
  if( m_conn_type[con] == STAGE_SYNC ) m_sync_counter--;
  
  m_pose_connection_count--;
  
  // shift the rest of the array 1 place left
  for( int p=con; p<m_pose_connection_count; p++ )
    {
      // the pollfd array
      memcpy( &(m_pose_connections[p]), 
	      &(m_pose_connections[p+1]),
	      sizeof( struct pollfd ) );
      
      // the connection type array
      memcpy( &(m_conn_type[p]), 
	      &(m_conn_type[p+1]),
	      sizeof( char ) );
   
      // the subscription type array
      memcpy( &(m_dirty_subscribe[p]), 
	      &(m_dirty_subscribe[p+1]),
	      sizeof( char ) );
    }      
  
  if( m_sync_counter < 1 && m_external_sync_required )
    m_enable = false;

#ifdef VERBOSE  
  printf( "Stage: remaining connections %d\n", m_pose_connection_count );
#endif
}



void CStageIO::HandleCommand( int con, cmd_t cmd )
{
  PRINT_DEBUG( "received command" );

  switch( cmd )
    {
      // TODO:
      //case LOADc: Load( m_filename ); break;

    case PAUSEc: // toggle simulation pause 
      m_enable = !m_enable; 
      break; 

    case SUBSCRIBEc: 
      PRINT_DEBUG1( "Received dirty: subscription on connection %d\n", con );
      m_dirty_subscribe[con] = 1; // this connection wants to receive deltas
      // tell the client which entities are subscribed by player clients
      //WriteSubscriptions( m_pose_connections[con].fd );
      break;

    case DOWNLOADc: // someone has requested a download of the world state
      PRINT_DEBUG( "DOWNLOADc" );
      
      WriteMatrix( m_pose_connections[con].fd );
      WriteBackground(  m_pose_connections[con].fd );
      WriteEntities( m_pose_connections[con].fd ); 
      WriteHeader(  m_pose_connections[con].fd, DownloadComplete, 0 );
      break;

    case SAVEc: // someone has asked us to save the world file
      PRINT_DEBUG( "SAVEc" );
      SaveFile(NULL); 
      break;

    default: printf( "Stage Warning: "
		     "Received unknown command (%d); ignoring.\n",
		     cmd );
    }
}


// read stuff until we get a continue message on each channel
int CStageIO::Read( void )
{
  //PRINT_DEBUG( "StageIO::Read()" );
  
  // if we have no connections, sleep until the timer signal goes off
  if( m_pose_connection_count == 0 )
    if( g_timer_expired < 1 ) sleep( 1 ); 
  
  // otherwise, check the connections for incoming stuff
  
  // if we have nothing to set the time, just increment it
  if( m_sync_counter == 0 ) m_step_num++;
  
  // in real time-mode, poll blocks until it is interrupted by
  // a timer signal, so we give it a time-out of -1. Otherwise,
  // we give it a zero time-out so it returns quickly.
  int timeout;
  m_real_timestep > 0 ? timeout = -1 : timeout = 0;
  
  int readable = 0;
  int syncs = 0;  
  
  // we loop on this poll until have the syncs. in realtime mode we
  // ALSO wait for the timer to run out
  while( m_pose_connection_count > 0 ) // as long as we have a connection
      {
        //printf( "polling with timeout: %d\n", timeout );
        // use poll to see which pose connections have data
        if((readable = 
  	  poll( m_pose_connections,
  		m_pose_connection_count,
  		timeout )) == -1) 
  	{
  	  if( errno == EINTR ) // interrupted by the real-time timer
  	    {
  	      //printf( "EINTR: syncs %d / %d\n",
	      //    syncs, m_sync_counter );
  	      // if we have all our syncs, we;re done
  	      if( syncs >= m_sync_counter )
  		return 0;
  	    }
  	  else
  	    {
  	      PRINT_ERR( "poll(2) returned error)");	  
  	      exit( -1 );
  	    }
  	}

        if( readable > 0 ) // if something was available
	  for( int t=0; t<m_pose_connection_count; t++ )// all the connections
	    {
	      short revents = m_pose_connections[t].revents;
	      
	      if( revents & POLLIN )// data available
		{ 
		  //printf( "poll() says data available (POLLIN)\n" );
		  
		  int hdrbytes;
		  stage_header_t hdr;
		  
		  hdrbytes = ReadHeader( m_pose_connections[t].fd, &hdr); 
		  
		  if( hdrbytes < (int)sizeof(hdr) )
		    {
		      printf( "Failed to read header on connection %d "
			      "(%d/%d bytes).\n"
			      "Connection closed",
			      t, hdrbytes, sizeof(hdr) );
		      
		      DestroyConnection( t ); // zap this connection
		    }
		  else
		    {
		      switch( hdr.type )
			{
			case PropertyPackets: // some poses are coming in 
			  PRINT_DEBUG2( "INCOMING PROPERTIES (%d) ON %d\n", 
					hdr.data, m_pose_connections[t].fd );
			  ReadProperties( t, m_pose_connections[t].fd, 
					  hdr.data );
			  break;
			
			case StageCommand:
			  HandleCommand( t, (cmd_t)hdr.data );
			  break;
			
			case DownloadComplete:
			  PRINT_DEBUG2( "DOWNLOAD COMPLETE (%d) on %d\n",  
					hdr.data, m_pose_connections[t].fd );
			  m_downloading = false;
			  return 0;
			  break;
			  
			case EntityPackets:
			  PRINT_DEBUG2( "INCOMING ENTITIES (%d) on %d\n", 
					hdr.data, m_pose_connections[t].fd );
			  ReadEntities( m_pose_connections[t].fd, hdr.data );
			  break;

			case MatrixPacket:
			   PRINT_DEBUG1( "MATRIX ON %d\n", 
				  m_pose_connections[t].fd );
			  ReadMatrix( m_pose_connections[t].fd );
			  break;

			case BackgroundPacket:
			  PRINT_DEBUG1( "BACKGROUND ON %d\n", 
					m_pose_connections[t].fd );
			  
			  ReadBackground( m_pose_connections[t].fd );
			  break;

			case Continue: // this marks the end of the data
			   PRINT_DEBUG1( "CONTINUE ON %d\n", 
				  m_pose_connections[t].fd );
			  
			  syncs++;
			  m_step_num=hdr.data;   // set the step number
			  
			  //printf( "syncs: %d/%d timer: %d\n", 
			  //    syncs, m_sync_counter, g_timer_expired );
			  
			  // if that's all the syncs and the timer is up,
			  //we're done
			  if( syncs >= m_sync_counter && g_timer_expired > 0 ) 
			    return 0;
			  
			  break;
			default:
			  printf( "Stage warning: unknown mesg type %d\n",
				  hdr.type);
			}
		    }
		}
	      // if poll reported some badness on this fd
	      else if( !revents & EINTR ) //
		{
		  printf( "Stage: connection %d seems bad\n", t );
		  DestroyConnection( t ); // zap this connection
		}    
	    }
        
	// if we're not realtime we can bail now...
        if( m_real_timestep == 0 ) break;
	//printf( "end\n" );
      } 
    
    return 0;
}
