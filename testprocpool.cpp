/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>


#include "spprocmanager.hpp"
#include "spprocpool.hpp"

//-------------------------------------------------------------------
class SP_EchoWorker : public SP_ProcWorker {
public:
	SP_EchoWorker(){}
	virtual ~SP_EchoWorker(){}

	virtual void process( SP_ProcInfo * procInfo ) {
		for( ; ; ) {
			char buff[ 256 ] = { 0 };
			int len = read( procInfo->getPipeFd(), buff, sizeof( buff ) );
			if( len > 0 ) {
				char newBuff[ 256 ] = { 0 };
				snprintf( newBuff, sizeof( newBuff ), "<%d> %s", (int)getpid(), buff );
				write( procInfo->getPipeFd(), newBuff, strlen( newBuff ) );
			} else {
				break;
			}

		}
	}
};

class SP_EchoWorkerFactory : public SP_ProcWorkerFactory {
public:
	SP_EchoWorkerFactory(){}
	virtual ~SP_EchoWorkerFactory(){}

	virtual SP_ProcWorker * create() const {
		return new SP_EchoWorker();
	}
};

//-------------------------------------------------------------------

int ii = 0;

void * echoWorkerCaller( void * args )
{
	const char * text = "Hello, world!";

	SP_ProcPool * procPool = (SP_ProcPool*)args;

	SP_ProcInfo * info;

	int n = 0;
	while(1)
	{
		info = procPool->get();
		if( (n = write( info->getPipeFd(), text, strlen( text ) )) > 0 ) {
			char buff[ 256 ] = { 0 };
			memset( buff, 0, sizeof( buff ) );
			printf( "write: %d - %d\n", (int)info->getPid(), n );
			int n = read( info->getPipeFd(), buff, sizeof( buff ) - 1 );
			if( n > 0 ) {
				printf( "read: %d - %s\n", (int)info->getPid(), buff );
			}
			else
			{
				if (!n)
					printf("read: %d end, %d\n", (int)info->getPid(), n);
				else
				{
					strerror_r(errno, buff, 256);
					printf("read: %d end, continue %d:%s\n", (int)info->getPid(), n, buff);
					continue;
				}
			}
			ii++;
			break;
		}
		else {
			printf("write error:%s", strerror(errno));
		}
	}
	procPool->save( info );

	return NULL;
}

int main( int argc, char * argv[] )
{
#ifdef LOG_PERROR
	openlog( "testprocpool", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "testprocpool", LOG_CONS | LOG_PID, LOG_USER );
#endif

	printf("pid:%d\n", getpid());
	SP_ProcManager manager( new SP_EchoWorkerFactory() );
	manager.start();

	signal(SIGPIPE,SIG_IGN);
	SP_ProcPool * procPool = manager.getProcPool();

	static int MAX_TEST_THREAD = 340;

	pthread_t threadArray[ MAX_TEST_THREAD ];

	for( int i = 0; i < MAX_TEST_THREAD; i++ ) {
		pthread_create( &( threadArray[i] ), NULL, echoWorkerCaller, procPool );
	}

	for( int i = 0; i < MAX_TEST_THREAD; i++ ) {
		pthread_join( threadArray[i], NULL );
	}

	printf("total:%d\n",ii);
	procPool->dump();

	closelog();
//	sleep(2);
	printf("total:%d\n",ii);
	return 0;
}

