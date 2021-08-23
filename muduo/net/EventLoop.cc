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

const int kPollTimeMs = 10000;                                                          //poll/epoll�ȴ���ʱʱ��

int createEventfd()
{
  //eventfd��һ���Ƚϸ�Ч���̼߳�ͨ�Ż��ƣ�����������ȫ����buffer����pipe����һ��pipe descriptor
  //֧��read��write��poll,epoll,select ,close�Ȳ���
  // ����һ���¼�fd��ר���������� poll/epoll
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();//�������κ���������ֱ����ֹ����
  }
  return evtfd;
}

//��һ���Ѿ��յ�FIN����socket����read����,
//������ջ����ѿ�, �򷵻�0, ����ǳ�˵�ı�ʾ���ӹر�.
//����һ�ζ������write����ʱ, ������ͻ���û����, �᷵����ȷд��(����).
//�����͵ı��Ļᵼ�¶Զ˷���RST����, ��Ϊ�Զ˵�socket�Ѿ�������close, ��ȫ�ر�, �Ȳ�����, Ҳ����������. ����,
//�ڶ��ε���write����(�������յ�RST֮��), ������SIGPIPE�ź�, ���½����˳�.

//Ϊ�˱�������˳�, ���Բ���SIGPIPE�ź�, ���ߺ�����, ��������SIG_IGN�źŴ�����:
//����, �ڶ��ε���write����ʱ, �᷵��-1, ͬʱerrno��ΪSIGPIPE. �������֪���Զ��Ѿ��ر�.
//close shutdown diff��https://www.cnblogs.com/JohnABC/p/7238241.html



/*
����(����)��:ָ��ĳ����������
int a __attribute__ ((unused));
ָ���ñ���Ϊ��δʹ�õġ�.��ʹ�������û�б�ʹ��,����ʱҲ���������������.


�ļ���:��Դ�����ļ������(����/����)
�﷨:
    #pragma GCC diagnostic [error|warning|ignored] "-W<����ѡ��>"
���-����:(�رվ���)
    #pragma  GCC diagnostic ignored  "-Wunused"
    #pragma  GCC diagnostic ignored  "-Wunused-parameter"

���-����:(��������)
    #pragma  GCC diagnostic warning  "-Wunused"
    #pragma  GCC diagnostic warning  "-Wunused-parameter"
���-����:(��������-����Ϊ����)
    #pragma  GCC diagnostic error  "-Wunused"
    #pragma  GCC diagnostic error  "-Wunused-parameter"
�÷�:
    ���ļ���ͷ���رվ���,���ļ���β���ٿ�������,�������Ժ��Ը��ļ��е�ָ������.


��Ŀ��:������/�������ָ��
����:
gcc main.c -Wall ����:
gcc mian.c -Wall -Wno-unused-parameter //��ȥall����,���Ǻ��� -unused-parameter����

ѡ���ʽ: -W[no-]<����ѡ��>
�� : -Wno-unused-parameter # no- ��ʾ���ʱ�����������

*/
#pragma GCC diagnostic ignored "-Wold-style-cast"
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
                                                                                    //LOG_TRACE << "Ignore SIGPIPE";
                                                                                    //����SIGPIPE�ź�
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
  : looping_(false),                                                                //��ʾ��δѭ��
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()),
    poller_(Poller::newDefaultPoller(this)),                                        //�����˻�������MUDUO_USE_POLL���͹���һ��ʵ�ʵ�PollPoller���󡣷�����һ��EPollPoller����
                                                                                    //����ָ�룬ָ��������,�����ָ�롢���ÿ���ָ���������
                                                                                    //poller_��Ա��eventlooper��ֻ����û����е��ĸ�������poll��updateChannel��removeChannel��hasChannel����������д��ǰ��������

    timerQueue_(new TimerQueue(this)),                                              //����һ��timerQueueָ�룬ʹ��scope_ptr����
    wakeupFd_(createEventfd()),                                                     //����eventfd��Ϊ�̼߳�ȴ�/֪ͨ����
    wakeupChannel_(new Channel(this, wakeupFd_)),                                   //����wakeupChannelͨ��
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  if (t_loopInThisThread)                                                           //��֤ÿ���߳����һ��EventLoop��������Ѵ�������ֹ����(LOG_FATAL)
  {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    t_loopInThisThread = this;
  }
  // �ϳ�һ��eventfd��ͨ��Channel
  // ���ö��¼��ص��������趨wakeupChannel�Ļص���������EventLoop�Լ��ĵ�handleRead����
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd

  wakeupChannel_->enableReading();                                                 // ʹ��wakeupFD_�������¼����˴�����Channel��enableReading����
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
 * �¼�ѭ��
 */
void EventLoop::loop()
{
  assert(!looping_);                                                              // �ж��Ƿ��ظ���ʼ�¼�ѭ��
  assertInLoopThread();                                                           //���Դ��ڴ����ö�����߳���
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  while (!quit_)
  {
    activeChannels_.clear();

    // 1.�ȴ��¼�
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);              //����poll���ػ��ͨ�����п����ǻ��ѷ��ص�

    // ��¼ѭ������
    ++iteration_;

    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority���������ȼ���ͨ������

    // 2.�����¼�
    eventHandling_ = true;
    for (Channel* channel : activeChannels_)                                    //����ͨ�������д���
    {
      currentActiveChannel_ = channel;                                          //��ǰ���ڴ���Ļͨ��
      currentActiveChannel_->handleEvent(pollReturnTime_);                      // ִ���¼���Ӧ�Ĵ���������Channel.cc��
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;

    // 3.����δִ�еĺ��� todo ��ʱ��֪������ҵ�񳡾�ʹ������ȽϺ���
    //I/O�߳���ƱȽ���ͨ������������Ҳ�ܹ����м������񣬷���I/O���Ǻܷ�æ��ʱ�����I/O�߳̾�һֱ��������״̬��
    //������Ҫ����Ҳ��ִ��һЩ��������
    doPendingFunctors();//�����û��ص�����
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

void EventLoop::runInLoop(Functor cb)                                           //��I/O�߳��е���ĳ���������ú������Կ��̵߳���
{
  if (isInLoopThread())                                                         // ��ǰִ�е��߳� �� eventloop�Ŀ����̲߳ſ�ֱ��ִ��cb()
  {
    cb();
  }
  else
  {
    // 1.�����ǰ�̲߳���eventloop�Ŀ����̣߳����첽��cb���뵽eventloop�ĺ��������У�
    // 2.������ eventloop�Ŀ����߳�
    queueInLoop(std::move(cb));
  }
}

/**
 * ���ص��������뵽����������
 */
void EventLoop::queueInLoop(Functor cb)
{
  {
  MutexLockGuard lock(mutex_);  // �ػ������뿪���������Զ��ͷ���
  pendingFunctors_.push_back(std::move(cb));
  }

  //�����ǰ����queueInLoop�Ĳ���I/O�̣߳���ô���Ѹ�I/O�̣߳��Ա�I/O�̼߳�ʱ����
  //���ߵ��õ��߳��ǵ�ǰI/O�̣߳����Ҵ�ʱ����pendingfunctor����Ҫ���ѣ���loop�ٴ�ȥpoll��ص�ʱ��ͻᷢ���Լ�������
  //ֻ�е�ǰI/O�̵߳��¼��ص��е���queueInLoop�Ų���Ҫ����
  if (!isInLoopThread() || callingPendingFunctors_)  // todo eventloop�Ƿ��ڴ������ǲ���Ӧ�ö���Ҫ���ѣ�
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
  ssize_t n = sockets::write(wakeupFd_, &one, sizeof one);                              //���д�����ݽ�ȥ�ͻ�����I/O�̣߳�I/O�̱߳����Ѻ󣬾��������Ĵ������̣�����EventLoop::handlRead
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}
//������Ϊ�˵�������EventLoop��queue��Ҳ����
//pendingFunctors_������������������ʱ��ͨ��eventfd֪ͨI/O�̴߳�poll״̬�˳���ִ��I/O��������
void EventLoop::handleRead()                                                           //��wakeupFD_����Ķ��¼��������ݶ�����֮��������
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
                                                                                     //�ٽ���������ʹ����һ��ջ�ϱ���functors��pendingFunctors����
  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
  }

                                                                                     //�˴������߳̾Ϳ�����pendingFunctors�������,��ʱ����Ҫ�ٽ籣��
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

