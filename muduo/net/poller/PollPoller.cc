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
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);           //系统调用poll
  int savedErrno = errno;
  Timestamp now(Timestamp::now());                                                  //时间戳
  if (numEvents > 0)                                                                //说明有事件发生
  {
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);                                  //放入活跃事件通道中
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
                                    ChannelList* activeChannels) const             //向活跃事件通道数组放入活跃事件
{
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)                               //遍历寻找产生事件的fd
  {
    if (pfd->revents > 0)                                                          //>=0 说明产生了事件
    {
      --numEvents;
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      channel->set_revents(pfd->revents);                                         //把监听到发生的事件放到Channel中
      // pfd->revents = 0;
      activeChannels->push_back(channel);                                         //加入活跃事件数组，该数组在EventLoop的成员变量之中
    }
  }
}

void PollPoller::updateChannel(Channel* channel)                                  //用于注册或者更新通道
{
  Poller::assertInLoopThread();                                                   //断言只能在I/O线程中调用
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0)                                                       //此时的channel还没有和事件绑定，表示这个channel是新增的
  {
    // a new one, add to pollfds_
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());                          //更新events
    pfd.revents = 0;                                                             //将已发生的revents清空
    pollfds_.push_back(pfd);                                                     //在pollfds数组末尾新增一个pollfd
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);                                                     //idx是channel的成员变量
    channels_[pfd.fd] = channel;                                                 //将channel放到map中
  }
  else
  {
    // update existing one
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);                                 //断言查看     channels_ map 的channel是否和传入的channel       一致
    int idx = channel->index();                                                  //该idx就是pollfds_数组的下标
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());                          //更新events
    pfd.revents = 0;                                                             //将已发生的revents清空
    if (channel->isNoneEvent())                                                  //channel中的events_为0
    {
      // ignore this pollfd
                                                                                 //暂时忽略该文件描述符的时间
                                                                                 //这里pfd.fd可以直接设置为-1
      pfd.fd = -channel->fd()-1;                                                 //将pollfds_元素的fd设置为负数，还可以被还原。这样子设置是为了removeChannel优化
    }
  }
}

void PollPoller::removeChannel(Channel* channel)                                //这个是真正移除pollfd的程序
{
  Poller::assertInLoopThread();                                                 //断言只能在I/O线程中调用
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent());                                              //channel中的events_为0
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());      //被删除的pollfd的fd要为负数，events要为0
  size_t n = channels_.erase(channel->fd());                                  //使用键值从map中移除fd和channel
  assert(n == 1); (void)n;
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)                        //移除pollfds_数组中的pollfd
  {
    pollfds_.pop_back();                                                      //pollfs_数组的最后一个，直接pop_back
  }
  else                                                                        //vector删除中间的元素，此处采用交换
  {
    int channelAtEnd = pollfds_.back().fd;                                    //先获取最后一个元素的fd
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);                        //被删除元素和最后一个元素交换
    if (channelAtEnd < 0)                                                     //如果pollfds_最后一个元素的fd小于0（因为events为0，因此fd被设置为负数）
    {
      channelAtEnd = -channelAtEnd-1;                                         //将pollfds_最后一个元素的fd还原一下，才能去channel中找到对应的键值
    }
    channels_[channelAtEnd]->set_index(idx);                                  //交换元素后，pollfds_最后一个元素的下标已经变成了idx，需要更新channel的index_
    pollfds_.pop_back();
  }
}

