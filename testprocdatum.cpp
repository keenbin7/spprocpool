/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <assert.h>
#include <iostream>
using namespace std;
#include "spprocdatum.hpp"
#include "spprocpdu.hpp"
#include "spprocpool.hpp"

int ii = 0;

class SP_ProcEchoService : public SP_ProcDatumService {
public:
	SP_ProcEchoService(){}

	virtual ~SP_ProcEchoService(){}

	virtual void handle( const SP_ProcDataBlock * request) {
		char buff[ 512 ] = { 0 };
		snprintf( buff, sizeof( buff ), "worker #%d - %s", (int)getpid(), (char*)request->getData() );
		ii++;
//		printf("[%d]in handle %d:%s\n",getpid(), ii, buff);
//		sleep(2);
	}
};

class SP_ProcEchoServiceFactory : public SP_ProcDatumServiceFactory {
public:
	SP_ProcEchoServiceFactory() {}
	virtual ~SP_ProcEchoServiceFactory() {}

	virtual SP_ProcDatumService * create() const {
		return new SP_ProcEchoService();
	}
};

int main( int argc, char * argv[] )
{
#ifdef LOG_PERROR
	openlog( "testprocdatum", LOG_CONS | LOG_PID | LOG_PERROR, LOG_USER );
#else
	openlog( "testprocdatum", LOG_CONS | LOG_PID, LOG_USER );
#endif

	SP_ProcArgs_t args = {10, 5, 2};

	SP_ProcDatumDispatcher dispatcher(&args);
	bool n =dispatcher.init( new SP_ProcEchoServiceFactory());

	if (!n)
	{
		cout<<"Diapatcher init fail"<<endl;
		exit(-1);
	}

//	dispatcher.getProcPool()->setMaxRequestsPerProc( 2 );

	char buff[ 256 ] = { 0 };
	for( int i = 0; i < 1000; i++ ) {
		snprintf( buff, sizeof( buff ), "Index %d, Hello world!", i );

		pid_t pid = 0;
		while((pid = dispatcher.dispatch( buff, strlen( buff ) )) == -1)
		{
//			cout<<"---------------process too much-------------------"<<endl;
			usleep(2000);
		}
//		printf( "dispatch %d to worker #%d\n", i, (int)pid );
	}

	printf( "wait 5 seconds for all processes to complete\n" );

	dispatcher.dump();
	cout<<"count:"<<dispatcher.getCount()<<endl;
	cout<<"idle1:"<<dispatcher.getIdleCount()<<endl;
	sleep( 10 );
	cout<<"count:"<<dispatcher.getCount()<<endl;
	cout<<"idle1:"<<dispatcher.getIdleCount()<<endl;

	dispatcher.dump();
//	sleep(10);

	closelog();
	cout<<"total:"<<ii<<endl;

	return 0;
}

