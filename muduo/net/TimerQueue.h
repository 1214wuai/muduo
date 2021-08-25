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
  /// Must be thread safe. Usually be called from other threads.                    //�̰߳�ȫ��
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);                                                     //���Կ��̵߳���

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.

  //��������set����˵���������ͬ�Ķ��������Ƕ�ʱ����ֻ��������ʽ��ͬ.  ��������Timer����ʱ����ͬ�����
  typedef std::pair<Timestamp, Timer*> Entry;                                       //set��key����һ��ʱ����Ͷ�ʱ����ַ��pair
  typedef std::set<Entry> TimerList;                                                //����ʱ�������
  typedef std::pair<Timer*, int64_t> ActiveTimer;                                   //��ʱ����ַ�����
  typedef std::set<ActiveTimer> ActiveTimerSet;                                     //���ն�ʱ����ַ����


  //���³�Ա����ֻ��������������I/O�߳��е��ã�������ؼ���
  //����������ɱ��֮һ������������Ҫ��������ʹ����
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();                                                                //��ʱ���¼������ص�����
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);                                     //���س�ʱ�Ķ�ʱ���б�
  void reset(const std::vector<Entry>& expired, Timestamp now);                     //�Գ�ʱ�Ķ�ʱ���������ã���Ϊ��ʱ�Ķ�ʱ���������ظ��Ķ�ʱ��

  bool insert(Timer* timer);                                                        //���붨ʱ��

  EventLoop* loop_;                                                                 //������event_loop
  const int timerfd_;                                                               //timefd_create()�������Ķ�ʱ��������
  Channel timerfdChannel_;                                                          //timefd_create()�������Ķ�ʱ��������?
  // Timer list sorted by expiration
  TimerList timers_;                                                                //��ʱ��set����ʱ�������

  // for cancel()
  ActiveTimerSet activeTimers_;                                                     //��Ծ��ʱ���б�����ʱ����ַ����
   bool callingExpiredTimers_; /* atomic */                                         //�Ƿ��ڵ��ô���ʱ��ʱ������
   ActiveTimerSet cancelingTimers_;                                                 //������Ǳ�ȡ���Ķ�ʱ��
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
