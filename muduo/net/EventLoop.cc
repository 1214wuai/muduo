// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/EventLoop.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Mutex.h"
#include "muduo/net/Channel.h"
#include "muduo/net/Poller.h"
#include "muduo/net/SocketsOps.h"
#include "muduo/net/TimerQueue.h"

#include <algorithm>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{
__thread EventLoop* t_loopInThisThread = 0;

const int kPollTimeMs = 10000;                                                          //poll/epoll等待超时时间

int createEventfd()
{
  //eventfd是一个比较高效的线程间通信机制，缓冲区管理全部用buffer，比pipe少用一个pipe descriptor
  //支持read，write，poll,epoll,select ,close等操作
  // 创建一个事件fd，专门用来唤起 poll/epoll
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();//不进行任何清理工作，直接终止程序
  }
  return evtfd;
}

//对一个已经收到FIN包的socket调用read方法,
//如果接收缓冲已空, 则返回0, 这就是常说的表示连接关闭.
//但第一次对其调用write方法时, 如果发送缓冲没问题, 会返回正确写入(发送).
//但发送的报文会导致对端发送RST报文, 因为对端的socket已经调用了close, 完全关闭, 既不发送, 也不接收数据. 所以,
//第二次调用write方法(假设在收到RST之后), 会生成SIGPIPE信号, 导致进程退出.

//为了避免进程退出, 可以捕获SIGPIPE信号, 或者忽略它, 给它设置SIG_IGN信号处理函数:
//这样, 第二次调用write方法时, 会返回-1, 同时errno置为SIGPIPE. 程序便能知道对端已经关闭.
//close shutdown diff：https://www.cnblogs.com/JohnABC/p/7238241.html



/*
变量(代码)级:指定某个变量警告
int a __attribute__ ((unused));
指定该变量为”未使用的”.即使这个变量没有被使用,编译时也会忽略则个警告输出.


文件级:在源代码文件中诊断(忽略/警告)
语法:
    #pragma GCC diagnostic [error|warning|ignored] "-W<警告选项>"
诊断-忽略:(关闭警告)
    #pragma  GCC diagnostic ignored  "-Wunused"
    #pragma  GCC diagnostic ignored  "-Wunused-parameter"

诊断-警告:(开启警告)
    #pragma  GCC diagnostic warning  "-Wunused"
    #pragma  GCC diagnostic warning  "-Wunused-parameter"
诊断-错误:(开启警告-升级为错误)
    #pragma  GCC diagnostic error  "-Wunused"
    #pragma  GCC diagnostic error  "-Wunused-parameter"
用法:
    在文件开头处关闭警告,在文件结尾出再开启警告,这样可以忽略该文件中的指定警告.


项目级:命令行/编译参数指定
警告:
gcc main.c -Wall 忽略:
gcc mian.c -Wall -Wno-unused-parameter //开去all警告,但是忽略 -unused-parameter警告

选项格式: -W[no-]<警告选项>
如 : -Wno-unused-parameter # no- 表示诊断时忽略这个警告

*/
#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
                                                                                    //LOG_TRACE << "Ignore SIGPIPE";
                                                                                    //忽略SIGPIPE信号
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}  // namespace

EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),                                                                //表示还未循环
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)),                                        //设置了环境变量MUDUO_USE_POLL，就构造一个实际的PollPoller对象。否则构造一个EPollPoller对象。
                                                                                    //基类指针，指向派生类,基类的指针、引用可以指向子类对象
                                                                                    //poller_成员在eventlooper中只会调用基类有的四个函数：poll、updateChannel、removeChannel、hasChannel。派生类重写了前三个函数

    timerQueue_(new TimerQueue(this)),                                              //构造一个timerQueue指针，使用scope_ptr管理
    wakeupFd_(createEventfd()),                                                     //创建eventfd作为线程间等待/通知机制
    wakeupChannel_(new Channel(this, wakeupFd_)),                                   //创建wakeupChannel通道
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread)                                                           //保证每个线程最多一个EventLoop对象，如果已创建，终止程序(LOG_FATAL)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }
  // 合成一个eventfd的通道Channel
  // 设置读事件回调函数，设定wakeupChannel的回调函数，即EventLoop自己的的handleRead函数
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd

  wakeupChannel_->enableReading();                                                 // 使能wakeupFD_监听读事件，此处调用Channel的enableReading函数
}

EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;
}

/**
 * 事件循环
 */
void EventLoop::loop()
{
  assert(!looping_);                                                              // 判断是否重复开始事件循环
  assertInLoopThread();                                                           //断言处于创建该对象的线程中
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();

    // 1.等待事件
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);              //调用poll返回活动的通道，有可能是唤醒返回的

    // 记录循环次数
    ++iteration_;

    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority（按照优先级对通道排序）

    // 2.处理事件
    eventHandling_ = true;
    for (Channel* channel : activeChannels_)                                    //遍历通道来进行处理
    {
      currentActiveChannel_ = channel;                                          //当前正在处理的活动通道
      currentActiveChannel_->handleEvent(pollReturnTime_);                      // 执行事件对应的处理函数，在Channel.cc中
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;

    // 3.处理未执行的函数 todo 暂时不知道哪种业务场景使用这个比较合适
    //I/O线程设计比较灵活，通过下面这个设计也能够进行计算任务，否则当I/O不是很繁忙的时候，这个I/O线程就一直处于阻塞状态。
    //我们需要让它也能执行一些计算任务
    doPendingFunctors();//处理用户回调任务
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

void EventLoop::quit()
{
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  if (!isInLoopThread())
  {
    wakeup();
  }
}

void EventLoop::runInLoop(Functor cb)                                           //在I/O线程中调用某个函数，该函数可以跨线程调用
{
  if (isInLoopThread())                                                         // 当前执行的线程 是 eventloop的控制线程才可直接执行cb()
  {
    cb();
  }
  else
  {
    // 1.如果当前线程不是eventloop的控制线程，则异步将cb加入到eventloop的函数队列中，
    // 2.并唤醒 eventloop的控制线程
    queueInLoop(std::move(cb));
  }
}

/**
 * 将回调函数加入到函数队列中
 */
void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);  // 守护锁，离开作用域后会自动释放锁
  pendingFunctors_.push_back(std::move(cb));
  }

  //如果当前调用queueInLoop的不是I/O线程，那么唤醒该I/O线程，以便I/O线程及时处理。
  //或者调用的线程是当前I/O线程，并且此时调用pendingfunctor，需要唤醒，当loop再次去poll监控的时候就会发现自己被唤醒
  //只有当前I/O线程的事件回调中调用queueInLoop才不需要唤醒
  if (!isInLoopThread() || callingPendingFunctors_)  // todo eventloop是否在处理函数是不是应该都需要唤醒？
  {
    wakeup();
  }
}

size_t EventLoop::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return pendingFunctors_.size();
}

TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

void EventLoop::updateChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

void EventLoop::wakeup()
{
  uint64_t one = 1;
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);                              //随便写点数据进去就唤醒了I/O线程，I/O线程被唤醒后，就走正常的处理流程，调用EventLoop::handlRead
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}
//这里是为了当我们向EventLoop的queue中也就是
//pendingFunctors_这个容器数组加入任务时，通过eventfd通知I/O线程从poll状态退出来执行I/O计算任务。
void EventLoop::handleRead()                                                           //被wakeupFD_唤起的读事件，将数据读出来之后不做处理
{
  uint64_t one = 1;
  ssize_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

void EventLoop::doPendingFunctors()
{
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
                                                                                     //临界区，这里使用了一个栈上变量functors和pendingFunctors交换
  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
  }

                                                                                     //此处其它线程就可以往pendingFunctors添加任务,此时不需要临界保护
  for (const Functor& functor : functors)
  {
    functor();
  }
  callingPendingFunctors_ = false;
}

void EventLoop::printActiveChannels() const
{
  for (const Channel* channel : activeChannels_)
  {
    LOG_TRACE << "{" << channel->reventsToString() << "} ";
  }
}

