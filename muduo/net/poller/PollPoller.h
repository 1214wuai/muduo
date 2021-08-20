// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

/*
����IOģ�ͣ�
    ����IO��������IO���ź�����IO��IO��·ת�ӣ��첽IO

    select
    �ó����غܶ���������״̬�仯
    #include<sys/select.h>
    int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
    ����ֵ��ִ�гɹ�����״̬�ı���¼�������0��ʾ��ʱ��-1��ʾ����������errno

    poll
    #include<poll.h>
    int poll(struct pollfd *fds, nfds_t nfds, int timeout);
          �ڶ���������ʾfds����ĳ���
    //pollfd�ṹ
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

  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;              //����ͨ���б�
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
