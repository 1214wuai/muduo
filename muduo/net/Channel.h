// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include "muduo/base/noncopyable.h"
#include "muduo/base/Timestamp.h"

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

class EventLoop;

///
/// A selectable I/O channel.
///
/// This class doesn't own the file descriptor.
/// The file descriptor could be a socket,
/// an eventfd, a timerfd, or a signalfd

/*
负责注册读写事件的类，并保存了fd读写事件发生时调用的回调函数，
如果poll/epoll有读写事件发生则将这些事件添加到对应的通道中。

>一个通道对应唯一EventLoop，一个EventLoop可以有多个通道。
>Channel类不负责fd的生存期，fd的生存期是有socket决定的，断开连接关闭描述符。
>当有fd返回读写事件时，调用提前注册的回调函数处理读写事件
*/
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback;                              //事件回调处理
  typedef std::function<void(Timestamp)> ReadEventCallback;                 //读事件的回调处理，传一个时间戳

  Channel(EventLoop* loop, int fd);
  ~Channel();

  void handleEvent(Timestamp receiveTime);                                  //处理事件
  void setReadCallback(ReadEventCallback cb)                                //设置各种回调函数
  { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  void tie(const std::shared_ptr<void>&);                                   //与TcpConnection有关，防止事件被销毁。

  int fd() const { return fd_; }
  int events() const { return events_; }                                    //注册的事件
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }           //判断是否无关注事件类型，events为0

  void enableReading() { events_ |= kReadEvent; update(); }           //或上事件，就是关注可读事件，注册到EventLoop，通过它注册到Poller中
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }                                           //pollfd数组中的下标
  void set_index(int idx) { index_ = idx; }

  // for debug
  string reventsToString() const;
  string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }
  void remove();                                                            //移除，确保调用前调用disableall

 private:
  static string eventsToString(int fd, int ev);

  void update();
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_;                                                        //记录所属Eventloop
  const int  fd_;                                                          //文件描述符，但不负责关闭该描述符
  int        events_;                                                      //关注的事件类型
  int        revents_; // it's the received event types of epoll or poll
  int        index_; // used by Poller.
  bool       logHup_;

  std::weak_ptr<void> tie_;                                                //负责生存期控制
  bool tied_;
  bool eventHandling_;                                                     //是否处于处理事件中
  bool addedToLoop_;
  ReadEventCallback readCallback_;                                         //几种事件处理回调
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
