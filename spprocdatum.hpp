/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#ifndef __spprocdatum_hpp__
#define __spprocdatum_hpp__

#include <unistd.h>
#include <pthread.h>
#include <sys/poll.h>

class SP_ProcPool;
class SP_ProcManager;
class SP_ProcDatumServiceFactory;
class SP_ProcDataBlock;
class SP_ProcInfo;



typedef struct tagSP_ProcArgs {
	int mMaxProc;
	int mMaxIdleProc;
	int mMinIdleProc;
} SP_ProcArgs_t;

class SP_ProcInfoListEx;

class SP_ProcDatumDispatcher {
public:
	SP_ProcDatumDispatcher( SP_ProcArgs_t *args );
	bool init( SP_ProcDatumServiceFactory * factory);
	~SP_ProcDatumDispatcher();

	// get the proc pool object to set parameters
	SP_ProcPool * getProcPool();

	// > 0 : Success, < 0 : fail, reach MaxProc limit or cannot get a process
	pid_t dispatch( const void * request, size_t len );

	void dump() const;
	int getCount();
	int getIdleCount();

private:
	// manager side
	SP_ProcManager * mManager;

	// app side
	SP_ProcPool * mPool;

	int mIsStop;
	pthread_mutex_t mMutex;
	pthread_cond_t mCond;

	SP_ProcInfoListEx * mListAll;

	SP_ProcArgs_t mArgs;

	static void * checkReply( void * );
};

//-------------------------------------------------------------------

class SP_ProcDatumService {
public:
	virtual ~SP_ProcDatumService();
	virtual void handle( const SP_ProcDataBlock * request) = 0;
};

class SP_ProcDatumServiceFactory {
public:
	virtual ~SP_ProcDatumServiceFactory();

	virtual SP_ProcDatumService * create() const = 0;

	virtual void workerInit( const SP_ProcInfo * procInfo );

	virtual void workerEnd( const SP_ProcInfo * procInfo );
};

#endif

