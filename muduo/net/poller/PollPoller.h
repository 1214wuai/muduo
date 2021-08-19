// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

/*
五种IO模型：
    阻塞IO，非阻塞IO，信号驱动IO，IO多路转接，异步IO

    select
    让程序监控很多描述符的状态变化
    #include<sys/select.h>
    int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

    poll
    #include<poll.h>
    int poll(struct pollfd *fds, nfds_t nfds, int timeout);
          第二个参数表示fds数组的长度
    //pollfd结构
    struct pollfd{
      int fd;
      short events;
      short revents;
    };

*/


#ifndef MUDUO_NET_POLLER_POLLPOLLER_H
#define MUDUO_NET_POLLER_POLLPOLLER_H

#include "muduo/net/Poller.h"

#include <vector>

struct pollfd;

namespace muduo
{
namespace net
{

///
/// IO Multiplexing with poll(2).
///
class PollPoller : public Poller
{
 public:

  PollPoller(EventLoop* loop);
  ~PollPoller() override;

  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;              //返回通道列表
  void updateChannel(Channel* channel) override;
  void removeChannel(Channel* channel) override;

 private:
  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;

  typedef std::vector<struct pollfd> PollFdList;
  PollFdList pollfds_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_POLLER_POLLPOLLER_H
