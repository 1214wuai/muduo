// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/Socket.h"

#include "muduo/base/Logging.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/SocketsOps.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;
//RAII
Socket::~Socket()
{
  sockets::close(sockfd_);
}

bool Socket::getTcpInfo(struct tcp_info* tcpi) const
{
  socklen_t len = sizeof(*tcpi);
  memZero(tcpi, len);
  return ::getsockopt(sockfd_, SOL_TCP, TCP_INFO, tcpi, &len) == 0;
}

bool Socket::getTcpInfoString(char* buf, int len) const
{
  struct tcp_info tcpi;
  bool ok = getTcpInfo(&tcpi);
  if (ok)
  {
    snprintf(buf, len, "unrecovered=%u "
             "rto=%u ato=%u snd_mss=%u rcv_mss=%u "
             "lost=%u retrans=%u rtt=%u rttvar=%u "
             "sshthresh=%u cwnd=%u total_retrans=%u",
             tcpi.tcpi_retransmits,  // Number of unrecovered [RTO] timeouts
             tcpi.tcpi_rto,          // Retransmit timeout in usec
             tcpi.tcpi_ato,          // Predicted tick of soft clock in usec
             tcpi.tcpi_snd_mss,
             tcpi.tcpi_rcv_mss,
             tcpi.tcpi_lost,         // Lost packets
             tcpi.tcpi_retrans,      // Retransmitted packets out
             tcpi.tcpi_rtt,          // Smoothed round trip time in usec
             tcpi.tcpi_rttvar,       // Medium deviation
             tcpi.tcpi_snd_ssthresh,
             tcpi.tcpi_snd_cwnd,
             tcpi.tcpi_total_retrans);  // Total retransmits for entire connection
  }
  return ok;
}

void Socket::bindAddress(const InetAddress& addr)
{
  sockets::bindOrDie(sockfd_, addr.getSockAddr());
}

void Socket::listen()
{
  sockets::listenOrDie(sockfd_);
}

int Socket::accept(InetAddress* peeraddr)
{
  struct sockaddr_in6 addr;
  memZero(&addr, sizeof addr);
  int connfd = sockets::accept(sockfd_, &addr);
  if (connfd >= 0)
  {
    peeraddr->setSockAddrInet6(addr);
  }
  return connfd;
}

void Socket::shutdownWrite()
{
  sockets::shutdownWrite(sockfd_);
}

void Socket::setTcpNoDelay(bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY,
               &optval, static_cast<socklen_t>(sizeof optval));
  //若为TRUE, 就会在套接字上禁用Nagle算法 (只适用于流式套接字)
  // FIXME CHECK
}

//地址复用，服务器挂掉之后需要立即重启，此时就需要地址复用
void Socket::setReuseAddr(bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,  // SO_REUSEADDR复用地址
               &optval, static_cast<socklen_t>(sizeof optval));
  //如果是TRUE，套接字就可与一个正由其他套接字使用的地址绑定到一起，或与处在TIME_WAIT状态的地址绑定到一起
  // FIXME CHECK
}

void Socket::setReusePort(bool on)
{
//SO_REUSEPORT是支持多个进程或者线程绑定到同一端口，提高服务器程序的吞吐性能
//解决问题：
//允许多个套接字 bind()/listen() 同一个TCP/UDP端口
//每一个线程拥有自己的服务器套接字
//在服务器套接字上没有了锁的竞争，因为每个进程一个服务器套接字
//内核层面实现负载均衡
//安全层面，监听同一个端口的套接字只能位于同一个用户下面
#ifdef SO_REUSEPORT
  int optval = on ? 1 : 0;
  int ret = ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT,
                         &optval, static_cast<socklen_t>(sizeof optval));
  if (ret < 0 && on)
  {
    LOG_SYSERR << "SO_REUSEPORT failed.";
  }
#else
  if (on)
  {
    LOG_ERROR << "SO_REUSEPORT is not supported.";
  }
#endif
}

void Socket::setKeepAlive(bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE,
               &optval, static_cast<socklen_t>(sizeof optval));
  //如果TRUE，套接字就会进行配置，在会话过程中发送”保持活动”消息
  //KeepAlive通过定时发送探测包来探测连接的对端是否存活

  /*
  常见的几种使用场景：
    检测挂掉的连接（导致连接挂掉的原因很多，如服务停止、网络波动、宕机、应用重启等）
    防止因为网络不活动而断连（使用NAT代理或者防火墙的时候，经常会出现这种问题）
    TCP层面的心跳检测


  一般我们使用KeepAlive时会修改空闲时长，避免资源浪费，系统内核会为每一个TCP连接
  建立一个保护记录，相对于应用层面效率更高。
  在Linux中我们可以通过修改 /etc/sysctl.conf 的全局配置：7200s就是两个小时

    net.ipv4.tcp_keepalive_time=7200
    net.ipv4.tcp_keepalive_intvl=75
    net.ipv4.tcp_keepalive_probes=9
  KeepAlive默认情况下是关闭的，可以被上层应用开启和关闭
tcp_keepalive_time: KeepAlive的空闲时长，或者说每次正常发送心跳的周期，默认值为7200s（2小时）
tcp_keepalive_intvl: KeepAlive探测包的发送间隔，默认值为75s
tcp_keepalive_probes: 在tcp_keepalive_time之后，没有接收到对方确认，继续发送保活探测包次数，默认值为9（次）

  和Http中Keep-Alive的关系
    HTTP协议的Keep-Alive意图在于连接复用，同一个连接上串行方式传递请求-响应数据
    TCP的KeepAlive机制意图在于保活、心跳，检测连接错误

  */
  // FIXME CHECK
}

