// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include "muduo/base/Mutex.h"
#include "muduo/base/CurrentThread.h"
#include "muduo/base/Timestamp.h"
#include "muduo/net/Callbacks.h"
#include "muduo/net/TimerId.h"

namespace muduo
{
namespace net
{

class Channel;
class Poller;
class TimerQueue;

///
/// Reactor, at most one per thread.每个线程最多只能有一个EventLoop对象
///
/// This is an interface class, so don't expose too much details.
//创建了EventLoop对象的线程称为IO线程，其功能是运行事件循环（EventLoop::loop）
//没有创建EventLoop对象的线程就不是IO线程
class EventLoop : noncopyable
{
 public:
  typedef std::function<void()> Functor;

  EventLoop();
  ~EventLoop();  // force out-line dtor, for std::unique_ptr members.

  ///
  /// Loops forever.
  ///
  /// Must be called in the same thread as creation of the object.
  ///
  void loop();

  /// Quits loop.
  ///
  /// This is not 100% thread safe, if you call through a raw pointer,
  /// better to call through shared_ptr<EventLoop> for 100% safety.
  void quit();

  ///
  /// Time when poll returns, usually means data arrival.
  ///
  Timestamp pollReturnTime() const { return pollReturnTime_; }

  int64_t iteration() const { return iteration_; }

  /// Runs callback immediately in the loop thread.
  /// It wakes up the loop, and run the cb.
  /// If in the same loop thread, cb is run within the function.
  /// Safe to call from other threads.
  void runInLoop(Functor cb);
  /// Queues callback in the loop thread.
  /// Runs after finish pooling.
  /// Safe to call from other threads.
  void queueInLoop(Functor cb);

  size_t queueSize() const;

  // timers

  ///
  /// Runs callback at 'time'.
  /// Safe to call from other threads.
  ///
  TimerId runAt(Timestamp time, TimerCallback cb);
  ///
  /// Runs callback after @c delay seconds.
  /// Safe to call from other threads.
  ///
  TimerId runAfter(double delay, TimerCallback cb);
  ///
  /// Runs callback every @c interval seconds.
  /// Safe to call from other threads.
  ///
  TimerId runEvery(double interval, TimerCallback cb);
  ///
  /// Cancels the timer.
  /// Safe to call from other threads.
  ///
  void cancel(TimerId timerId);

  // internal usage
  void wakeup();
  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);
  bool hasChannel(Channel* channel);

  // pid_t threadId() const { return threadId_; }
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      abortNotInLoopThread();
    }
  }
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  // bool callingPendingFunctors() const { return callingPendingFunctors_; }
  bool eventHandling() const { return eventHandling_; }

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  static EventLoop* getEventLoopOfCurrentThread();

 private:
  void abortNotInLoopThread();
  void handleRead();  // waked up
  void doPendingFunctors();

  void printActiveChannels() const; // DEBUG

  typedef std::vector<Channel*> ChannelList;

  bool looping_; /* atomic */                   // 当前事件循环是否正在运行
  std::atomic<bool> quit_;                      //是否退出标志
  bool eventHandling_; /* atomic */             //是否在处理事件标志
  bool callingPendingFunctors_; /* atomic */    //是否调用pendingFunctors标志
  int64_t iteration_;                           //迭代器
  const pid_t threadId_;                        //当前所属对象线程id
  Timestamp pollReturnTime_;                    //时间戳，poll返回的时间戳
  std::unique_ptr<Poller> poller_;              //poller对象
  std::unique_ptr<TimerQueue> timerQueue_;      //TimerQueue类型对象指针，构造函数中new
  int wakeupFd_;                                //用于eventfd，线程间通信
  // unlike in TimerQueue, which is an internal class,
  // we don't expose Channel to client.
  // 专用于唤醒 poll/epoll
  std::unique_ptr<Channel> wakeupChannel_;      //wakeupfd所对应的通道，该通道会纳入到poller_来管理
  boost::any context_;

  // scratch variables
  ChannelList activeChannels_;                  //Poller返回的活动通道，vector<channel*>类型
  Channel* currentActiveChannel_;               //当前正在处理的活动通道

  // 保证pendingFunctors_线程安全
  mutable MutexLock mutex_;
  // 未执行（等待执行）的函数列表
  std::vector<Functor> pendingFunctors_ GUARDED_BY(mutex_); //本线程或其它线程使用queueInLoop添加的任务，可能是I/O计算任务
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOP_H
