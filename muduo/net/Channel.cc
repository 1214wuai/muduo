// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Logging.h"
#include "muduo/net/Channel.h"
#include "muduo/net/EventLoop.h"

#include <sstream>

#include <poll.h>

using namespace muduo;
using namespace muduo::net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop* loop, int fd__)
  : loop_(loop),
    fd_(fd__),
    events_(0),
    revents_(0),
    index_(-1),                                                             //index_初始值设置为-1，表示本channel目前还没有和pollfd_绑定
    logHup_(true),
    tied_(false),
    eventHandling_(false),
    addedToLoop_(false)
{
}

Channel::~Channel()
{
  assert(!eventHandling_);
  assert(!addedToLoop_);
  if (loop_->isInLoopThread())
  {
    assert(!loop_->hasChannel(this));
  }
}

//延长对象的生命期，此处延长了TcpConnection的生命期，使之长过Channel::handleEvent()函数
void Channel::tie(const std::shared_ptr<void>& obj)                         //有新连接到来时，在TcpConnection的connectEstablished函数中调用，进行生存期控制
{
  tie_ = obj;                                                               //是把TcpConnection型智能指针存入了Channel之中,tie_是weak_ptr
  tied_ = true;
}

void Channel::update()                                                      //更新事件类型
{
  addedToLoop_ = true;
  loop_->updateChannel(this);
}

void Channel::remove()                                                      //移除channel，最终到epoll中移除
{
  assert(isNoneEvent());
  addedToLoop_ = false;
  loop_->removeChannel(this);
}
                                                                            //处理所有发生的事件，如果活着，底层调用handleEventWithGuard
void Channel::handleEvent(Timestamp receiveTime)                            //事件到来调用handleEvent处理
{
  /*
   * RAII，对象管理资源
   * weak_ptr使用lock提升成shared_ptr，此时引用计数加一
   * 函数返回，栈空间对象销毁，提升的shared_ptr guard销毁，引用计数减一
   */
  std::shared_ptr<void> guard;                                              //保证线程安全，保证不会调用一个销毁了的对象
  if (tied_)                                                                //这个标志位与TcpConnection有关，在连接建立的时候，该标志位被设置为true，
  {
    guard = tie_.lock();                                                    //如果此时tie_管理的对象不为空，则返回一个shared_ptr，并且保证给tie_赋值的指针不被释放，此时引用计数变为2
    if (guard)
    {
      LOG_INFO << "Before handleEventWithGurd use_count = " << guard.use_count();
      handleEventWithGuard(receiveTime);
      LOG_INFO << "After handleEventWithGurd use_count = " << guard.use_count();
    }
  }
  else
  {
    handleEventWithGuard(receiveTime);
  }
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
  eventHandling_ = true;
  LOG_TRACE << reventsToString();
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN))                         //判断返回事件类型
  {
    if (logHup_)                                                            //如果有POLLHUP事件(常见于写端被关闭，在读端会受到POLLHUP)，输出警告信息
    {
      LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
    }
    if (closeCallback_) closeCallback_();                                  //调用关闭回调函数
  }

  if (revents_ & POLLNVAL)                                                 //不合法文件描述符
  {
    LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
  }

  if (revents_ & (POLLERR | POLLNVAL))
  {
    if (errorCallback_) errorCallback_();
  }
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))                             //POLLRDHUP是对端关闭连接事件，如shutdown等
  {
    if (readCallback_) readCallback_(receiveTime);
  }
  if (revents_ & POLLOUT)
  {
    if (writeCallback_) writeCallback_();
  }
  eventHandling_ = false;//处理完了=false
}

string Channel::reventsToString() const
{
  return eventsToString(fd_, revents_);
}

string Channel::eventsToString() const
{
  return eventsToString(fd_, events_);
}

string Channel::eventsToString(int fd, int ev)                              //调试输出发生了什么事件
{
  std::ostringstream oss;
  oss << fd << ": ";
  if (ev & POLLIN)
    oss << "IN ";
  if (ev & POLLPRI)
    oss << "PRI ";
  if (ev & POLLOUT)
    oss << "OUT ";
  if (ev & POLLHUP)
    oss << "HUP ";
  if (ev & POLLRDHUP)
    oss << "RDHUP ";
  if (ev & POLLERR)
    oss << "ERR ";
  if (ev & POLLNVAL)
    oss << "NVAL ";

  return oss.str();
}
