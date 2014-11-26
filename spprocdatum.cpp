/*
 * Copyright 2007 Stephen Liu
 * For license terms, see the file COPYING along with this library.
 */

#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <signal.h>

#include "spprocdatum.hpp"

#include "spprocmanager.hpp"
#include "spprocpool.hpp"
#include "spprocpdu.hpp"
SP_ProcDatumService ::~SP_ProcDatumService()
{
}

//-------------------------------------------------------------------

SP_ProcDatumServiceFactory::~SP_ProcDatumServiceFactory()
{
}

void SP_ProcDatumServiceFactory::workerInit(const SP_ProcInfo * procInfo)
{
}

void SP_ProcDatumServiceFactory::workerEnd(const SP_ProcInfo * procInfo)
{
}

//-------------------------------------------------------------------

class SP_ProcWorkerDatumAdapter: public SP_ProcWorker
{
public:
	SP_ProcWorkerDatumAdapter(SP_ProcDatumServiceFactory * factory);
	virtual ~SP_ProcWorkerDatumAdapter();

	virtual void process(SP_ProcInfo * procInfo);

private:
	SP_ProcDatumServiceFactory * mFactory;
};

SP_ProcWorkerDatumAdapter::SP_ProcWorkerDatumAdapter(
		SP_ProcDatumServiceFactory * factory)
{
	mFactory = factory;
}

SP_ProcWorkerDatumAdapter::~SP_ProcWorkerDatumAdapter()
{
}

void SP_ProcWorkerDatumAdapter::process(SP_ProcInfo * procInfo)
{
	mFactory->workerInit(procInfo);

	for (;;)
	{
		SP_ProcDataBlock request;
		SP_ProcPdu_t pdu;
		memset(&pdu, 0, sizeof(pdu));

		if (SP_ProcPduUtils::read_pdu(procInfo->getPipeFd(), &pdu, &request) > 0)
		{
			SP_ProcDatumService * service = mFactory->create();
			service->handle(&request);
			delete service;

			SP_ProcPdu_t replyPdu;
			memset(&replyPdu, 0, sizeof(SP_ProcPdu_t));
			replyPdu.mMagicNum = SP_ProcPdu_t::MAGIC_NUM;
			replyPdu.mSrcPid = getpid();
			replyPdu.mDestPid = pdu.mSrcPid;
			replyPdu.mDataSize = 0;

			if (SP_ProcPduUtils::send_pdu(procInfo->getPipeFd(), &replyPdu, NULL) < 0)
			{
				break;
			}
		}
		else
		{
			break;
		}
	}

	mFactory->workerEnd(procInfo);
}

//-------------------------------------------------------------------

class SP_ProcWorkerFactoryDatumAdapter: public SP_ProcWorkerFactory
{
public:
	SP_ProcWorkerFactoryDatumAdapter(SP_ProcDatumServiceFactory * factory);
	virtual ~SP_ProcWorkerFactoryDatumAdapter();

	virtual SP_ProcWorker * create() const;

private:
	SP_ProcDatumServiceFactory * mFactory;
};

SP_ProcWorkerFactoryDatumAdapter::SP_ProcWorkerFactoryDatumAdapter(
		SP_ProcDatumServiceFactory * factory)
{
	mFactory = factory;
}

SP_ProcWorkerFactoryDatumAdapter::~SP_ProcWorkerFactoryDatumAdapter()
{
	delete mFactory;
	mFactory = NULL;
}

SP_ProcWorker * SP_ProcWorkerFactoryDatumAdapter::create() const
{
	return new SP_ProcWorkerDatumAdapter(mFactory);
}

//-------------------------------------------------------------------

class SP_ProcInfoListEx
{
public:
	SP_ProcInfoListEx(int maxProc);
	~SP_ProcInfoListEx();

	void append(SP_ProcInfo * info);
	SP_ProcInfo * getByPipeFd(int pipeFd);
	SP_ProcInfo * getIdle(SP_ProcPool * pool);

	SP_ProcInfo * getIdle();

	bool erase(SP_ProcInfo * pInfo, SP_ProcPool * pool);

	int conv2pollfd(struct pollfd pfd[], int nfds);

	int getCount();

	int getIdleCount();

	void dump();

private:
	void _append(SP_ProcInfo * info);
	int mMaxProc;
	pthread_mutex_t mMutex;
	pthread_cond_t mCond;
	pthread_cond_t mEmptyCond;

	SP_ProcInfoList * mList;
};

SP_ProcInfoListEx::SP_ProcInfoListEx(int maxProc)
{
	mMaxProc = maxProc;
	pthread_mutex_init(&mMutex, NULL);
	pthread_cond_init(&mCond, NULL);
	pthread_cond_init(&mEmptyCond, NULL);

	mList = new SP_ProcInfoList();
}

SP_ProcInfoListEx::~SP_ProcInfoListEx()
{
	pthread_mutex_destroy(&mMutex);
	pthread_cond_destroy(&mCond);
	pthread_cond_destroy(&mEmptyCond);

	delete mList;
	mList = NULL;
}

void SP_ProcInfoListEx::append(SP_ProcInfo * info)
{
	pthread_mutex_lock(&mMutex);

	_append(info);

	pthread_mutex_unlock(&mMutex);
}

void SP_ProcInfoListEx::_append(SP_ProcInfo * info)
{
	mList->append(info);

	if (1 == mList->getCount())
		pthread_cond_signal(&mCond);
}

SP_ProcInfo * SP_ProcInfoListEx::getIdle(SP_ProcPool * pool)
{
	SP_ProcInfo *pInfo = NULL;
	pthread_mutex_lock(&mMutex);

	SP_ProcInfo *pTmp = NULL;
	for (int i = 0; i < mList->getCount(); i++)
	{
		pTmp = mList->getItem(i);
		if (SP_ProcInfo::CHAR_IDLE == mList->getItem(i)->isIdle())
		{
			pInfo = pTmp;
			break;
		}
	}

	if (!pInfo)
	{
		if (mList->getCount() < mMaxProc)
		{
			pInfo = pool->get();
			if(pInfo)
			{
				pInfo->setIdle(SP_ProcInfo::CHAR_BUSY);
				_append(pInfo);
			}
		}
	}
	else
	{
		pInfo->setIdle(SP_ProcInfo::CHAR_BUSY);
	}

	pthread_mutex_unlock(&mMutex);
	return pInfo;
}

SP_ProcInfo * SP_ProcInfoListEx::getIdle()
{
	SP_ProcInfo *pInfo = NULL;
	pthread_mutex_lock(&mMutex);

	for (int i = 0; i < mList->getCount(); i++)
	{
		pInfo = mList->getItem(i);
		if (SP_ProcInfo::CHAR_IDLE == mList->getItem(i)->isIdle())
		{
			break;
		}
	}

	if (pInfo)
	{
		pInfo->setIdle(SP_ProcInfo::CHAR_BUSY);
	}
	pthread_mutex_unlock(&mMutex);
	return pInfo;
}

bool SP_ProcInfoListEx::erase(SP_ProcInfo * pInfo, SP_ProcPool * pool)
{
	bool b = false;
	pthread_mutex_lock(&mMutex);

	int i = 0;
	SP_ProcInfo *pTmp;
	for (; i < mList->getCount(); i++)
	{
		pTmp = mList->getItem(i);
		if (pTmp == pInfo)
		{
			b = true;
			break;
		}
	}
	if (b)
	{
		pTmp = mList->takeItem(i);
		pool->erase(pTmp);
	}

	pthread_mutex_unlock(&mMutex);
	return pInfo;


}

SP_ProcInfo * SP_ProcInfoListEx::getByPipeFd(int pipeFd)
{
	SP_ProcInfo * ret = NULL;

	pthread_mutex_lock(&mMutex);
	ret = mList->getItem(mList->findByPipeFd(pipeFd));
	if (mList->getCount() <= 0)
		pthread_cond_signal(&mEmptyCond);

	pthread_mutex_unlock(&mMutex);

	return ret;
}

int SP_ProcInfoListEx::conv2pollfd(struct pollfd pfd[], int nfds)
{
	pthread_mutex_lock(&mMutex);

	if (mList->getCount() <= 0)
	{
		syslog(LOG_INFO, "INFO: waiting for not empty");

		struct timezone tz;
		struct timeval now;
		gettimeofday(&now, &tz);

		struct timespec timeout;
		timeout.tv_sec = now.tv_sec + 5;
		timeout.tv_nsec = now.tv_usec * 1000;

		pthread_cond_timedwait(&mCond, &mMutex, &timeout);
	}

	nfds = mList->getCount() > nfds ? nfds : mList->getCount();

	for (int i = 0; i < nfds; i++)
	{
		const SP_ProcInfo * info = mList->getItem(i);

		pfd[i].fd = info->getPipeFd();
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
	}

	pthread_mutex_unlock(&mMutex);

	return nfds;
}

int SP_ProcInfoListEx::getCount()
{
	pthread_mutex_lock(&mMutex);
	int n = mList->getCount();
	pthread_mutex_unlock(&mMutex);
	return n;
}


int SP_ProcInfoListEx::getIdleCount()
{
	int n = 0;
	pthread_mutex_lock(&mMutex);
	SP_ProcInfo* pInfo;
	for (int i = 0; i< mList->getCount(); i++)
	{
		pInfo = mList->getItem(i);
		if (SP_ProcInfo::CHAR_IDLE == pInfo->isIdle())
			n++;
	}
	pthread_mutex_unlock(&mMutex);
	return n;
}

void SP_ProcInfoListEx::dump()
{
	pthread_mutex_lock(&mMutex);
	mList->dump();
	pthread_mutex_unlock(&mMutex);
}
//-------------------------------------------------------------------

SP_ProcDatumDispatcher::SP_ProcDatumDispatcher(SP_ProcArgs_t *args)
{
	mArgs = *args;

	mIsStop = 0;

	mListAll = new SP_ProcInfoListEx(mArgs.mMaxProc);

	pthread_mutex_init(&mMutex, NULL);
	pthread_cond_init(&mCond, NULL);

	mPool = NULL;
	mManager = NULL;
}

bool SP_ProcDatumDispatcher::init(SP_ProcDatumServiceFactory * factory)
{
	signal(SIGPIPE, SIG_IGN);
	mManager = new SP_ProcManager(new SP_ProcWorkerFactoryDatumAdapter(factory));
	mManager->start();

	mPool = mManager->getProcPool();

	mIsStop = 0;

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	assert( pthread_attr_setstacksize( &attr, 1024 * 1024 ) == 0);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	pthread_t thread = 0;
	int ret = pthread_create(&thread, &attr,
			reinterpret_cast<void*(*)(void*)>(checkReply), this );
	pthread_attr_destroy(&attr);
	if (0 == ret)
	{
		syslog(LOG_NOTICE, "Thread #%ld has been created to check reply", thread);
	}
	else
	{
		syslog(LOG_WARNING, "Unable to create a thread to check reply, errno %d, %s",
				errno, strerror(errno));
		return false;
	}

	//预先创建最小个数空闲进程
	int i = 0;
	for (; i < mArgs.mMinIdleProc; i++)
	{
		SP_ProcInfo * proc = mPool->get();

		if (!proc)
			return false;

//		mPool->save(proc);
		mListAll->append(proc);
	}

	return true;
}

SP_ProcDatumDispatcher::~SP_ProcDatumDispatcher()
{
	pthread_mutex_lock(&mMutex);
	mIsStop = 1;
	pthread_cond_wait(&mCond, &mMutex);
	pthread_mutex_unlock(&mMutex);

	delete mManager;
	mManager = NULL;

	delete mListAll;
	mListAll = NULL;
}

SP_ProcPool * SP_ProcDatumDispatcher::getProcPool()
{
	return mPool;
}

void * SP_ProcDatumDispatcher::checkReply(void * args)
{
	sigset_t sset;
	sigemptyset(&sset);
	sigaddset(&sset, SIGPIPE);
	sigaddset(&sset, SIGINT);
	sigaddset(&sset, SIGUSR1);
	sigaddset(&sset, SIGUSR2);
	sigaddset(&sset, SIGCLD);
	sigaddset(&sset, SIGCHLD);
	sigaddset(&sset, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &sset, 0);


	SP_ProcDatumDispatcher * dispatcher = (SP_ProcDatumDispatcher*) args;

	SP_ProcInfoListEx * list = dispatcher->mListAll;

	SP_ProcPool * pool = dispatcher->mPool;

	for (; !(dispatcher->mIsStop);)
	{
		// 1. collect fd
		const static int SP_PROC_MAX_FD = 1024;
		struct pollfd pfd[SP_PROC_MAX_FD];
		int nfds = list->conv2pollfd(pfd, SP_PROC_MAX_FD);

		if (0 == nfds)
			continue;

		// 2. select fd
		int ret = -1;
		for (int retry = 0; retry < 2; retry++)
		{
			ret = poll(pfd, nfds, 10);
			if (-1 == ret && EINTR == errno)
				continue;
			break;
		}

		// 3. dispatch reply/error
		if (ret > 0)
		{
			for (int i = 0; i < nfds; i++)
			{
				if (0 == pfd[i].revents)
				{
					// timeout, wait for next turn
				}
				else
				{
					SP_ProcInfo * info = list->getByPipeFd(pfd[i].fd);
					if (NULL == info)
					{
						syslog(LOG_CRIT, "CRIT: found a not exists fd %d, dangerous",
								pfd[i].fd);
						continue;
					}

					SP_ProcPdu_t pdu;
					SP_ProcDataBlock reply;

					if (pfd[i].revents & POLLIN)
					{
						// readable
						if (SP_ProcPduUtils::read_pdu(pfd[i].fd, &pdu, &reply) > 0)
						{
							info->setIdle(SP_ProcInfo::CHAR_IDLE);
						}
						else
						{
							list->erase(info, pool);
						}
					}
					else
					{
						list->erase(info, pool);
					}
				}
			}

			while (list->getIdleCount() > dispatcher->mArgs.mMaxIdleProc)
			{
				SP_ProcInfo *info = list->getIdle();
				list->erase(info, pool);
			}
		}
		else
		{
			// ignore
		}
	}

	pthread_mutex_lock(&(dispatcher->mMutex));
	pthread_cond_signal(&(dispatcher->mCond));
	pthread_mutex_unlock(&(dispatcher->mMutex));

	return NULL;
}

pid_t SP_ProcDatumDispatcher::dispatch(const void * request, size_t len)
{
	pid_t ret = -1;

	SP_ProcInfo * info = mListAll->getIdle(mPool);
	if (NULL != info)
	{
		SP_ProcPdu_t pdu;
		memset(&pdu, 0, sizeof(pdu));
		pdu.mMagicNum = SP_ProcPdu_t::MAGIC_NUM;
		pdu.mSrcPid = getpid();
		pdu.mDestPid = info->getPid();
		pdu.mDataSize = len;

		if (SP_ProcPduUtils::send_pdu(info->getPipeFd(), &pdu, request) > 0)
		{
			ret = info->getPid();
		}
		else
		{
			if(mListAll->erase(info, mPool))
			{
				syslog(LOG_INFO, "INFO: erase SP_ProcInfo");
			}
		}
	}

	return ret;
}

void SP_ProcDatumDispatcher::dump() const
{
	mListAll->dump();
}

int SP_ProcDatumDispatcher::getCount()
{
	return mListAll->getCount();
}

int SP_ProcDatumDispatcher::getIdleCount()
{
	return mListAll->getIdleCount();
}

