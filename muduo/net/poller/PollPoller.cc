// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/poller/PollPoller.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Channel.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller() = default;

Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);           //ϵͳ����poll
  int savedErrno = errno;
  Timestamp now(Timestamp::now());                                                  //ʱ���
  if (numEvents > 0)                                                                //˵�����¼�����
  {
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);                                  //�����Ծ�¼�ͨ����
  }
  else if (numEvents == 0)
  {
    LOG_TRACE << " nothing happened";
  }
  else
  {
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;
}

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const             //���Ծ�¼�ͨ����������Ծ�¼�
{
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)                               //����Ѱ�Ҳ����¼���fd���ҵ���ֵΪrevents���޸ĵ�������
  {
    if (pfd->revents > 0)                                                          //>=0 ˵���������¼�
    {
      --numEvents;
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      channel->set_revents(pfd->revents);                                         //�Ѽ������������¼��ŵ�Channel�У�channel����ʱ��״̬
      // pfd->revents = 0;
      activeChannels->push_back(channel);                                         //�����Ծ�¼����飬��������EventLoop�ĳ�Ա����֮��
    }
  }
}

void PollPoller::updateChannel(Channel* channel)                                  //����ע����߸���ͨ��
{
  Poller::assertInLoopThread();                                                   //����ֻ����I/O�߳��е���
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0)                                                       //��ʱ��channel��û�к��¼��󶨣���ʾ���channel��������
  {
    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());                          //����events
    pfd.revents = 0;                                                             //���ѷ�����revents���
    pollfds_.push_back(pfd);                                                     //��pollfds����ĩβ����һ��pollfd
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);                                                     //idx��channel�ĳ�Ա����
    channels_[pfd.fd] = channel;                                                 //��channel�ŵ�map��
  }
  else
  {
    // update existing one
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);                                 //���Բ鿴     channels_ map ��channel�Ƿ�ʹ����channel       һ��
    int idx = channel->index();                                                  //��idx����pollfds_������±�
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());                          //����events
    pfd.revents = 0;                                                             //���ѷ�����revents���
    if (channel->isNoneEvent())                                                  //channel�е�events_Ϊ0
    {
      // ignore this pollfd
                                                                                 //��ʱ���Ը��ļ���������ʱ��
                                                                                 //����pfd.fd����ֱ������Ϊ-1
      pfd.fd = -channel->fd()-1;                                                 //��pollfds_Ԫ�ص�fd����Ϊ�����������Ա���ԭ��������������Ϊ��removeChannel�Ż�
    }
  }
}

void PollPoller::removeChannel(Channel* channel)                                //����������Ƴ�pollfd�ĳ���
{
  Poller::assertInLoopThread();                                                 //����ֻ����I/O�߳��е���
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent());                                              //channel�е�events_Ϊ0
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());      //��ɾ����pollfd��fdҪΪ������eventsҪΪ0
  size_t n = channels_.erase(channel->fd());                                  //ʹ�ü�ֵ��map���Ƴ�fd��channel
  assert(n == 1); (void)n;
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)                        //�Ƴ�pollfds_�����е�pollfd
  {
    pollfds_.pop_back();                                                      //pollfs_��������һ����ֱ��pop_back
  }
  else                                                                        //vectorɾ���м��Ԫ�أ��˴����ý���
  {
    int channelAtEnd = pollfds_.back().fd;                                    //�Ȼ�ȡ���һ��Ԫ�ص�fd
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);                        //��ɾ��Ԫ�غ����һ��Ԫ�ؽ���
    if (channelAtEnd < 0)                                                     //���pollfds_���һ��Ԫ�ص�fdС��0����ΪeventsΪ0�����fd������Ϊ������
    {
      channelAtEnd = -channelAtEnd-1;                                         //��pollfds_���һ��Ԫ�ص�fd��ԭһ�£�����ȥchannel���ҵ���Ӧ�ļ�ֵ
    }
    channels_[channelAtEnd]->set_index(idx);                                  //����Ԫ�غ�pollfds_���һ��Ԫ�ص��±��Ѿ������idx����Ҫ����channel��index_
    pollfds_.pop_back();
  }
}

