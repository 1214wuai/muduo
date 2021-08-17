// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Thread.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Exception.h"
#include "muduo/base/Logging.h"

#include <type_traits>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <linux/unistd.h>
/*
 * glibc是GNU发布的libc库，即c运行库。
 * glibc是linux系统中最底层的api，几乎其它任何运行库都会依赖于glibc。
 *
 * 进程ID，在进程描述符中是pid_t tgid，
 *      含义是Thread Group ID,该值对应的是用户层面的进程ID
 *      可以通过getpid()获取
 * 线程ID，在进程描述符中是pid_t pid
 *      Linux提供了gettid系统调用来返回其线程ID，可是glibc并没有将该系统调用封装起来。需要调用如下接口来获取：
 *      #include <sys/syscall.h>
 *      pid_t tid;
 *      tid = syscall(SYS_gettid);
 *
 * 线程组内的第一个线程，在用户态被称为主线程(main thread),在内核中被称为group leader，
 * 内核在创建第一个线程时，会将线程组的ID的值设置成第一个线程的线程ID，
 * group_leader指针则指向自身，既主线程的进程描述符。
 *
 * 在Linux中，目前的线程实现是Native POSIX Thread Libaray,简称NPTL。
 * pthread_ create函数会产生一个线程ID，存放在第一个参数指向的地址中。该线程ID和前面说的线程ID不是一回事。
 *
 * 前面讲的线程ID属于进程调度的范畴。因为线程是轻量级进程，是操作系统调度器的最小单位，所以需要一个数值来唯一表示该线程。
 *
 * pthread_ create函数产生并标记在第一个参数指向的地址中的线程ID中，属于NPTL线程库的范
 * 畴。线程库的后续操作，就是根据该线程ID来操作线程的。
 *
 * 线程库NPTL提供了pthread_ self函数，可以获得线程自身的ID
 */
namespace muduo
{
namespace detail
{

pid_t gettid()
{
  return static_cast<pid_t>(::syscall(SYS_gettid));
  //有一个真实的线程id唯一标识，tid
  //通过linux下的系统调用syscall(SYS_gettid)来获得。
}
/*
 * #include <pthread.h>
 * int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void));
 * pthread_atfork()在fork()之前调用，
 * 当调用fork时，内部创建子进程前在父进程中会调用prepare，
 * 内部创建子进程成功后，父进程会调用parent ，子进程会调用child。
 */
void afterFork()
{
  muduo::CurrentThread::t_cachedTid = 0;
  muduo::CurrentThread::t_threadName = "main";
  CurrentThread::tid();
  // no need to call pthread_atfork(NULL, NULL, &afterFork);
}

class ThreadNameInitializer
{
 public:
  ThreadNameInitializer()
  {
    muduo::CurrentThread::t_threadName = "main";
    CurrentThread::tid();
    pthread_atfork(NULL, NULL, &afterFork);
  }
};

ThreadNameInitializer init;

struct ThreadData
{
  typedef muduo::Thread::ThreadFunc ThreadFunc;
  ThreadFunc func_;
  string name_;
  pid_t* tid_;
  CountDownLatch* latch_;

  ThreadData(ThreadFunc func,
             const string& name,
             pid_t* tid,
             CountDownLatch* latch)
    : func_(std::move(func)),
      name_(name),
      tid_(tid),
      latch_(latch)
  { }

  void runInThread()
  {
    *tid_ = muduo::CurrentThread::tid();
    tid_ = NULL;
    latch_->countDown();
    latch_ = NULL;

    muduo::CurrentThread::t_threadName = name_.empty() ? "muduoThread" : name_.c_str();
    ::prctl(PR_SET_NAME, muduo::CurrentThread::t_threadName);
    try
    {
      func_();
      muduo::CurrentThread::t_threadName = "finished";
    }
    catch (const Exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
      abort();
    }
    catch (const std::exception& ex)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      abort();
    }
    catch (...)
    {
      muduo::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
      throw; // rethrow
    }
  }
};

void* startThread(void* obj)
{
  ThreadData* data = static_cast<ThreadData*>(obj);
  data->runInThread();
  delete data;
  return NULL;
}

}  // namespace detail

void CurrentThread::cacheTid()
{
  if (t_cachedTid == 0)
  {
    t_cachedTid = detail::gettid();//获取线程ID，进程描述符结构体中的pid_t pid
    t_tidStringLength = snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
  }
}

bool CurrentThread::isMainThread()
{
  return tid() == ::getpid();  // getpid获取当前进程id，进程id等于主线程id
}

void CurrentThread::sleepUsec(int64_t usec)
{
  struct timespec ts = { 0, 0 };
  ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
  ::nanosleep(&ts, NULL);
}

AtomicInt32 Thread::numCreated_;

Thread::Thread(ThreadFunc func, const string& n)
  : started_(false),
    joined_(false),
    pthreadId_(0),
    tid_(0),
    func_(std::move(func)),
    name_(n),
    latch_(1)
{
  setDefaultName();
}

Thread::~Thread()
{
  if (started_ && !joined_)
  {
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName()
{
  int num = numCreated_.incrementAndGet();//记录已经创建的线程个数，，原子操作
  if (name_.empty())
  {
    char buf[32];
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

void Thread::start()
{
  assert(!started_);
  started_ = true;
  // FIXME: move(func_)
  detail::ThreadData* data = new detail::ThreadData(func_, name_, &tid_, &latch_);
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data))//成功返回0
  {
    started_ = false;
    delete data; // or no delete?
    LOG_SYSFATAL << "Failed in pthread_create";
  }
  else
  {
    latch_.wait();
    assert(tid_ > 0);
  }
}

int Thread::join()
{
  assert(started_);
  assert(!joined_);
  joined_ = true;
  return pthread_join(pthreadId_, NULL);
}

}  // namespace muduo
