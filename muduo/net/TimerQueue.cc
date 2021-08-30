// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include "muduo/net/TimerQueue.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/Timer.h"
#include "muduo/net/TimerId.h"

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{
/*
>>>sleep/alarm/usleep在实现时有可能用了信号SIGALRM，在多线程程序中处理信号是个相当麻烦的事情，应该尽量避免。

>>>nanosleep和clock_nanosleep是线程安全的，但是在非阻塞网络编程中，绝对不能用让线程挂起的方式来等待一段时间。程序会失去响应，正确的做法是注册一个时间回调函数。

>>>getitimer和timer_create也是用信号来deliver超时，在多线程程序中也会有麻烦。

>>>timer_create可以指定信号的接收方是进程还是线程，算是一个进步，不过在信号处理函数（signal handler）中能做的事情实在是很受限。

*/
int createTimerfd()
{
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}
/*
int timerfd_create(int clockid, int flags);
    参数1 clockid是指，CLOCK_REALTIME or CLOCK_MONOTONIC时间。前者是我们平时所看到的时间，后者是石英钟时间也就是晶振决定的时间
    参数2 有两个选项TFD_NONBLOCK和TFD_CLOEXEC

    timerfd_create把时间变成了一个文件描述符，该“文件”在定时器超时的那一刻变得可读，这样就能很方便的融入到select/poll框架中，
    用统一的方式来处理I/O和超时事件，这正是Reactor模式的长处


int timerfd_settime(int fd, int flags,
                         const struct itimerspec *new_value,
                         struct itimerspec *old_value);
    参数1是文件描述符。
    参数2是参加timerfd_create()函数。
    参数3是新值，itermerspec结构体有两个字段，解释见下面。
    参数4是旧值，结构体见下面。

           struct timespec {
               time_t tv_sec;                // Seconds
               long   tv_nsec;               // Nanoseconds
           };
*/

struct timespec howMuchTimeFromNow(Timestamp when)                                             //计算超时时刻与当前时间的时间差
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();                          //超时时刻微秒数-当前时间微秒数
  if (microseconds < 100)                                                                      //不能小于100，精确度不需要
  {
    microseconds = 100;
  }
  struct timespec ts;                                                                          //转换成这个结构体返回
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

void readTimerfd(int timerfd, Timestamp now)                                                  //从timerfd读取，避免定时器事件一直触发
{
  uint64_t howmany;
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);                                      //从timerfd读取4个字节，这样timerfd就不会一直触发了
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

void resetTimerfd(int timerfd, Timestamp expiration)                                          //重置定时器超时时刻
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  memZero(&newValue, sizeof newValue);
  memZero(&oldValue, sizeof oldValue);
  newValue.it_value = howMuchTimeFromNow(expiration);                                        //将时间戳类转换成it_value的形式
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);                             //设置进去，到期之后会产生一个定时器事件
  if (ret)
  {
    LOG_SYSERR << "timerfd_settime()";
  }
}

}  // namespace detail
}  // namespace net
}  // namespace muduo

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop),
    timerfd_(createTimerfd()),                                                              //创建定时器，调用timerfd_create，返回timerfd
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  timerfdChannel_.enableReading();                                                         //注册到poll中，需要被监听起来
}

TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  for (const Entry& timer : timers_)
  {
    delete timer.second;                                                                   //析构函数只释放一次，因为两个set保存的是一样的
  }
}

TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  Timer* timer = new Timer(std::move(cb), when, interval);                                //构造一个定时器对象，interval>0就是重复定时器
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));                               //将addTimerInLoop函数放到I/O线程的pendingFunctors_数组中去执行
  return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)                                                  //执行线程退出的回调函数
{
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();                                                            //判断是不是I/O线程在执行本函数
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
    resetTimerfd(timerfd_, timer->expiration());
  }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  ActiveTimerSet::iterator it = activeTimers_.find(timer);                              //查找该定时器
  if (it != activeTimers_.end())
  {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));                //删除该定时器//如果用unique_ptr这里就不需要手工删除了
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_)                                                       //如果在定时器列表中没有找到，可能已经到期，且正在处理的定时器
  {
    cancelingTimers_.insert(timer);                                                     //已经到期，并且正在调用回调函数的定时器
  }
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()                                                          //可读事件处理
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);                                                          //清除该事件，避免一直触发，实际上是对timerfd做了read

  std::vector<Entry> expired = getExpired(now);//获取该时刻之前所有的定时器列表，即超时定时器列表，因为实际上可能有多个定时器超时，存在定时器的时间设定是一样的这种情况

  callingExpiredTimers_ = true;                                                        //处于处理定时器状态中
  cancelingTimers_.clear();
  // safe to callback outside critical section
  for (const Entry& it : expired)
  {
    it.second->run();                                                                  //调用所有的run()函数，底层调用Timer类的设置了的超时回调函数
  }
  callingExpiredTimers_ = false;

  reset(expired, now);                                                                 //如果移除的不是一次性定时器，那么重新启动它们
}

//返回当前所有超时的定时器列表
//返回值由于rvo优化，不会拷贝构造vector，直接返回它
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  std::vector<Entry> expired;
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));                          //创建一个时间戳和定时器地址的集合

  //返回第一个未到期的Timer的迭代器
  //lower_bound返回第一个值>=sentry的
  //即*end>=sentry，从而end->first > now，而不是>=now，因为pair比较的第一个相等后会比较第二个，而sentry的第二个是UINTPTR_MAX最大
  //所以用lower_bound没有用upper_bound
  TimerList::iterator end = timers_.lower_bound(sentry);

  assert(end == timers_.end() || now < end->first);
  std::copy(timers_.begin(), end, back_inserter(expired));                          //将到期的定时器插入到expired中
  timers_.erase(timers_.begin(), end);                                              //删除已到期的所有定时器

  for (const Entry& it : expired)                                                   //从activeTimers_中也要移除到期的定时器
  {
    ActiveTimer timer(it.second, it.second->sequence());
    size_t n = activeTimers_.erase(timer);
    assert(n == 1); (void)n;
  }

  assert(timers_.size() == activeTimers_.size());
  return expired;
}

void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    if (it.second->repeat()                                                        //如果是重复的定时器，并且是未取消定时器，则重启该定时器
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it.second->restart(now);                                                     //restart()函数中会重新计算下一个超时时刻
      insert(it.second);
    }
    else
    {
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please                                //一次性定时器或者已被取消的定时器是不能重置的，因此删除该定时器
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();                           //获取最早到期的超时时间
  }

  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);                                          //重新设定timerfd的超时时间
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());                               //这两个存的是同样的定时器列表
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;                                                    //如果插入定时器时间小于最早到期时间
  }
  //下面两个插入的set保存的是一样的，都是定时器，只不过对组的另一个辅助成员不一样
  {

    //利用RAII机制
    //插入到timers_中，result是临时对象，需要用它来保证插入成功
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  {
    //插入到activeTimers中
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;                                                     //返回是否最早到期时间改变
}

