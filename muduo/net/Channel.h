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
����ע���д�¼����࣬��������fd��д�¼�����ʱ���õĻص�������
���poll/epoll�ж�д�¼���������Щ�¼���ӵ���Ӧ��ͨ���С�

>һ��ͨ����ӦΨһEventLoop��һ��EventLoop�����ж��ͨ����
>Channel�಻����fd�������ڣ�fd������������socket�����ģ��Ͽ����ӹر���������
>����fd���ض�д�¼�ʱ��������ǰע��Ļص����������д�¼�

һ��fd����һ��channel
*/
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback;                              //�¼��ص�����
  typedef std::function<void(Timestamp)> ReadEventCallback;                 //���¼��Ļص�������һ��ʱ���

  Channel(EventLoop* loop, int fd);
  ~Channel();

  void handleEvent(Timestamp receiveTime);                                  //�����¼�
  void setReadCallback(ReadEventCallback cb)                                //���ø��ֻص�����
  { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  void tie(const std::shared_ptr<void>&);                                   //��TcpConnection�йأ���ֹ�¼������١�

  int fd() const { return fd_; }
  int events() const { return events_; }                                    //ע����¼�
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }           //�ж��Ƿ��޹�ע�¼����ͣ�eventsΪ0

  void enableReading() { events_ |= kReadEvent; update(); }           //�����¼������ǹ�ע�ɶ��¼���ע�ᵽEventLoop��ͨ����ע�ᵽPoller��
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }                    //ȡ���������¼��ļ���
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }                                           //pollfd�����е��±�
  void set_index(int idx) { index_ = idx; }                                //����Ա����index_����ΪPollPoller��Ա����pollfds�����ĳ���±꣬��ʾ������¼���channel

  // for debug
  string reventsToString() const;
  string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }
  void remove();                                                            //�Ƴ���ȷ������ǰ����disableall

 private:
  static string eventsToString(int fd, int ev);

  void update();
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;                                             //0
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_;                                                        //��¼����Eventloop
  const int  fd_;                                                          //�ļ�����������������رո�������
  int        events_;                                                      //��ע���¼�����
  int        revents_; // it's the received event types of epoll or poll
  int        index_; // used by Poller.
  bool       logHup_;

  std::weak_ptr<void> tie_;                                                //���������ڿ���
  bool tied_;
  bool eventHandling_;                                                     //�Ƿ��ڴ����¼���
  bool addedToLoop_;
  ReadEventCallback readCallback_;                                         //�����¼�����ص�
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
