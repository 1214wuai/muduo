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
>>>sleep/alarm/usleep��ʵ��ʱ�п��������ź�SIGALRM���ڶ��̳߳����д����ź��Ǹ��൱�鷳�����飬Ӧ�þ������⡣

>>>nanosleep��clock_nanosleep���̰߳�ȫ�ģ������ڷ������������У����Բ��������̹߳���ķ�ʽ���ȴ�һ��ʱ�䡣�����ʧȥ��Ӧ����ȷ��������ע��һ��ʱ��ص�������

>>>getitimer��timer_createҲ�����ź���deliver��ʱ���ڶ��̳߳�����Ҳ�����鷳��

>>>timer_create����ָ���źŵĽ��շ��ǽ��̻����̣߳�����һ���������������źŴ�������signal handler��������������ʵ���Ǻ����ޡ�

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
    ����1 clockid��ָ��CLOCK_REALTIME or CLOCK_MONOTONICʱ�䡣ǰ��������ƽʱ��������ʱ�䣬������ʯӢ��ʱ��Ҳ���Ǿ��������ʱ��
    ����2 ������ѡ��TFD_NONBLOCK��TFD_CLOEXEC

    timerfd_create��ʱ������һ���ļ����������á��ļ����ڶ�ʱ����ʱ����һ�̱�ÿɶ����������ܷܺ�������뵽select/poll����У�
    ��ͳһ�ķ�ʽ������I/O�ͳ�ʱ�¼���������Reactorģʽ�ĳ���


int timerfd_settime(int fd, int flags,
                         const struct itimerspec *new_value,
                         struct itimerspec *old_value);
    ����1���ļ���������
    ����2�ǲμ�timerfd_create()������
    ����3����ֵ��itermerspec�ṹ���������ֶΣ����ͼ����档
    ����4�Ǿ�ֵ���ṹ������档

           struct timespec {
               time_t tv_sec;                // Seconds
               long   tv_nsec;               // Nanoseconds
           };
*/

struct timespec howMuchTimeFromNow(Timestamp when)                                             //���㳬ʱʱ���뵱ǰʱ���ʱ���
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();                          //��ʱʱ��΢����-��ǰʱ��΢����
  if (microseconds < 100)                                                                      //����С��100����ȷ�Ȳ���Ҫ
  {
    microseconds = 100;
  }
  struct timespec ts;                                                                          //ת��������ṹ�巵��
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

void readTimerfd(int timerfd, Timestamp now)                                                  //��timerfd��ȡ�����ⶨʱ���¼�һֱ����
{
  uint64_t howmany;
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);                                      //��timerfd��ȡ4���ֽڣ�����timerfd�Ͳ���һֱ������
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

void resetTimerfd(int timerfd, Timestamp expiration)                                          //���ö�ʱ����ʱʱ��
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  memZero(&newValue, sizeof newValue);
  memZero(&oldValue, sizeof oldValue);
  newValue.it_value = howMuchTimeFromNow(expiration);                                        //��ʱ�����ת����it_value����ʽ
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);                             //���ý�ȥ������֮������һ����ʱ���¼�
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
    timerfd_(createTimerfd()),                                                              //������ʱ��������timerfd_create������timerfd
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  timerfdChannel_.enableReading();                                                         //ע�ᵽpoll�У���Ҫ����������
}

TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  for (const Entry& timer : timers_)
  {
    delete timer.second;                                                                   //��������ֻ�ͷ�һ�Σ���Ϊ����set�������һ����
  }
}

TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  Timer* timer = new Timer(std::move(cb), when, interval);                                //����һ����ʱ������interval>0�����ظ���ʱ��
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));                               //��addTimerInLoop�����ŵ�I/O�̵߳�pendingFunctors_������ȥִ��
  return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)                                                  //ִ���߳��˳��Ļص�����
{
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();                                                            //�ж��ǲ���I/O�߳���ִ�б�����
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
  ActiveTimerSet::iterator it = activeTimers_.find(timer);                              //���Ҹö�ʱ��
  if (it != activeTimers_.end())
  {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));                //ɾ���ö�ʱ��//�����unique_ptr����Ͳ���Ҫ�ֹ�ɾ����
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_)                                                       //����ڶ�ʱ���б���û���ҵ��������Ѿ����ڣ������ڴ���Ķ�ʱ��
  {
    cancelingTimers_.insert(timer);                                                     //�Ѿ����ڣ��������ڵ��ûص������Ķ�ʱ��
  }
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()                                                          //�ɶ��¼�����
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  readTimerfd(timerfd_, now);                                                          //������¼�������һֱ������ʵ�����Ƕ�timerfd����read

  std::vector<Entry> expired = getExpired(now);//��ȡ��ʱ��֮ǰ���еĶ�ʱ���б�����ʱ��ʱ���б���Ϊʵ���Ͽ����ж����ʱ����ʱ�����ڶ�ʱ����ʱ���趨��һ�����������

  callingExpiredTimers_ = true;                                                        //���ڴ���ʱ��״̬��
  cancelingTimers_.clear();
  // safe to callback outside critical section
  for (const Entry& it : expired)
  {
    it.second->run();                                                                  //�������е�run()�������ײ����Timer��������˵ĳ�ʱ�ص�����
  }
  callingExpiredTimers_ = false;

  reset(expired, now);                                                                 //����Ƴ��Ĳ���һ���Զ�ʱ������ô������������
}

//���ص�ǰ���г�ʱ�Ķ�ʱ���б�
//����ֵ����rvo�Ż������´������vector��ֱ�ӷ�����
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  std::vector<Entry> expired;
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));                          //����һ��ʱ����Ͷ�ʱ����ַ�ļ���

  //���ص�һ��δ���ڵ�Timer�ĵ�����
  //lower_bound���ص�һ��ֵ>=sentry��
  //��*end>=sentry���Ӷ�end->first > now��������>=now����Ϊpair�Ƚϵĵ�һ����Ⱥ��Ƚϵڶ�������sentry�ĵڶ�����UINTPTR_MAX���
  //������lower_boundû����upper_bound
  TimerList::iterator end = timers_.lower_bound(sentry);

  assert(end == timers_.end() || now < end->first);
  std::copy(timers_.begin(), end, back_inserter(expired));                          //�����ڵĶ�ʱ�����뵽expired��
  timers_.erase(timers_.begin(), end);                                              //ɾ���ѵ��ڵ����ж�ʱ��

  for (const Entry& it : expired)                                                   //��activeTimers_��ҲҪ�Ƴ����ڵĶ�ʱ��
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
    if (it.second->repeat()                                                        //������ظ��Ķ�ʱ����������δȡ����ʱ�����������ö�ʱ��
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      it.second->restart(now);                                                     //restart()�����л����¼�����һ����ʱʱ��
      insert(it.second);
    }
    else
    {
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please                                //һ���Զ�ʱ�������ѱ�ȡ���Ķ�ʱ���ǲ������õģ����ɾ���ö�ʱ��
    }
  }

  if (!timers_.empty())
  {
    nextExpire = timers_.begin()->second->expiration();                           //��ȡ���絽�ڵĳ�ʱʱ��
  }

  if (nextExpire.valid())
  {
    resetTimerfd(timerfd_, nextExpire);                                          //�����趨timerfd�ĳ�ʱʱ��
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());                               //�����������ͬ���Ķ�ʱ���б�
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;                                                    //������붨ʱ��ʱ��С�����絽��ʱ��
  }
  //�������������set�������һ���ģ����Ƕ�ʱ����ֻ�����������һ��������Ա��һ��
  {

    //����RAII����
    //���뵽timers_�У�result����ʱ������Ҫ��������֤����ɹ�
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  {
    //���뵽activeTimers��
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;                                                     //�����Ƿ����絽��ʱ��ı�
}

