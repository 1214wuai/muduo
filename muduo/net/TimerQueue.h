// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include "muduo/base/Mutex.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/Channel.h"

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue : noncopyable
{
 public:
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.                    //线程安全的
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);                                                     //可以跨线程调用

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.

  //下面两个set可以说保存的是相同的东西，都是定时器，只不过排序方式不同.  处理两个Timer到期时间相同的情况
  typedef std::pair<Timestamp, Timer*> Entry;                                       //set的key，是一个时间戳和定时器地址的pair
  typedef std::set<Entry> TimerList;                                                //按照时间戳排序
  typedef std::pair<Timer*, int64_t> ActiveTimer;                                   //定时器地址和序号
  typedef std::set<ActiveTimer> ActiveTimerSet;                                     //按照定时器地址排序


  //以下成员函数只可能在其所属的I/O线程中调用，因而不必加锁
  //服务器性能杀手之一就是锁竞争，要尽可能少使用锁
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();                                                                //定时器事件产生回调函数
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);                                     //返回超时的定时器列表
  void reset(const std::vector<Entry>& expired, Timestamp now);                     //对超时的定时器进行重置，因为超时的定时器可能是重复的定时器

  bool insert(Timer* timer);                                                        //插入定时器

  EventLoop* loop_;                                                                 //所属的event_loop
  const int timerfd_;                                                               //timefd_create()所创建的定时器描述符
  Channel timerfdChannel_;                                                          //timefd_create()所创建的定时器描述符?
  // Timer list sorted by expiration
  TimerList timers_;                                                                //定时器set，按时间戳排序

  // for cancel()
  ActiveTimerSet activeTimers_;                                                     //活跃定时器列表，按定时器地址排序
   bool callingExpiredTimers_; /* atomic */                                         //是否处于调用处理超时定时器当中
   ActiveTimerSet cancelingTimers_;                                                 //保存的是被取消的定时器
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
