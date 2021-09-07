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
             tcpi.tcpi_retransmits,  // Number of unrecovered [RTO] timeouts      /* ��ʱ�ش��Ĵ��� */
             tcpi.tcpi_rto,          // Retransmit timeout in usec                /* ��ʱʱ�䣬��λΪ΢��*/
             tcpi.tcpi_ato,          // Predicted tick of soft clock in usec      /* ��ʱȷ�ϵĹ�ֵ����λΪ΢��*/
             tcpi.tcpi_snd_mss,                                                   /* ���˵�MSS */
             tcpi.tcpi_rcv_mss,                                                   /* �Զ˵�MSS */
             tcpi.tcpi_lost,         // Lost packets                              /* ��ʧ��δ�ָ������ݶ��� */
             tcpi.tcpi_retrans,      // Retransmitted packets out                 /* �ش���δȷ�ϵ����ݶ��� */
             tcpi.tcpi_rtt,          // Smoothed round trip time in usec          /* ƽ����RTT����λΪ΢�� */
             tcpi.tcpi_rttvar,       // Medium deviation                          /* �ķ�֮һmdev����λΪ΢��v */
             tcpi.tcpi_snd_ssthresh,                                              /* ��������ֵ */
             tcpi.tcpi_snd_cwnd,                                                  /* ӵ������ */
             tcpi.tcpi_total_retrans);  // Total retransmits for entire connection/* �����ӵ����ش����� */
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
  //��ΪTRUE, �ͻ����׽����Ͻ���Nagle�㷨 (ֻ��������ʽ�׽���)
  // FIXME CHECK
}

//��ַ���ã��������ҵ�֮����Ҫ������������ʱ����Ҫ��ַ����
void Socket::setReuseAddr(bool on)
{
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,  // SO_REUSEADDR���õ�ַ
               &optval, static_cast<socklen_t>(sizeof optval));
  //�����TRUE���׽��־Ϳ���һ�����������׽���ʹ�õĵ�ַ�󶨵�һ�𣬻��봦��TIME_WAIT״̬�ĵ�ַ�󶨵�һ��
  //����Ƿ����������Ͽ����ӣ�����TIME_WAIT����ʱ���ܰ󶨳ɹ���������SO_REUSEADDR���ͷŵĶ˿��ܹ������ٴ�ʹ��
  // FIXME CHECK
}

void Socket::setReusePort(bool on)
{
//SO_REUSEPORT��֧�ֶ�����̻����̰߳󶨵�ͬһ�˿ڣ���߷������������������
//������⣺
//�������׽��� bind()/listen() ͬһ��TCP/UDP�˿�
//ÿһ���߳�ӵ���Լ��ķ������׽���
//�ڷ������׽�����û�������ľ�������Ϊÿ������һ���������׽���
//�ں˲���ʵ�ָ��ؾ���
//��ȫ���棬����ͬһ���˿ڵ��׽���ֻ��λ��ͬһ���û�����
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
  //���TRUE���׽��־ͻ�������ã��ڻỰ�����з��͡����ֻ����Ϣ
  //KeepAliveͨ����ʱ����̽�����̽�����ӵĶԶ��Ƿ���

  /*
  �����ļ���ʹ�ó�����
    ���ҵ������ӣ��������ӹҵ���ԭ��ܶ࣬�����ֹͣ�����粨����崻���Ӧ�������ȣ�
    ��ֹ��Ϊ���粻���������ʹ��NAT������߷���ǽ��ʱ�򣬾���������������⣩
    TCP������������


  һ������ʹ��KeepAliveʱ���޸Ŀ���ʱ����������Դ�˷ѣ�ϵͳ�ں˻�Ϊÿһ��TCP����
  ����һ��������¼�������Ӧ�ò���Ч�ʸ��ߡ�
  ��Linux�����ǿ���ͨ���޸� /etc/sysctl.conf ��ȫ�����ã�7200s��������Сʱ

    net.ipv4.tcp_keepalive_time=7200
    net.ipv4.tcp_keepalive_intvl=75
    net.ipv4.tcp_keepalive_probes=9
  KeepAliveĬ��������ǹرյģ����Ա��ϲ�Ӧ�ÿ����͹ر�
tcp_keepalive_time: KeepAlive�Ŀ���ʱ��������˵ÿ�������������������ڣ�Ĭ��ֵΪ7200s��2Сʱ��
tcp_keepalive_intvl: KeepAlive̽����ķ��ͼ����Ĭ��ֵΪ75s
tcp_keepalive_probes: ��tcp_keepalive_time֮��û�н��յ��Է�ȷ�ϣ��������ͱ���̽���������Ĭ��ֵΪ9���Σ�

  ��Http��Keep-Alive�Ĺ�ϵ
    HTTPЭ���Keep-Alive��ͼ�������Ӹ��ã�ͬһ�������ϴ��з�ʽ��������-��Ӧ����
    TCP��KeepAlive������ͼ���ڱ��������������Ӵ���

  */
  // FIXME CHECK
}

