// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/net/SocketsOps.h"

#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include "muduo/net/Endian.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>  // snprintf
#include <sys/socket.h>
#include <sys/uio.h>  // readv
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

namespace
{

typedef struct sockaddr SA;

//fcntl�������ǰ��׽�������Ϊ������ʽI/O�ͻ����ź�����ʽI/O���Լ������׽���������POSIX�ķ�ʽ
//�˴���Ϊ��������closeexec

/*
clsoe on exec
�ر��ӽ��������ļ�������
��fork�ӽ��̺���Ȼ����ʹ��fd����ִ��exec��ϵͳ�ͻ��ֶιر��ӽ����е�fd��
*/
#if VALGRIND || defined (NO_ACCEPT4)
void setNonBlockAndCloseOnExec(int sockfd)
{
  // non-block
  int flags = ::fcntl(sockfd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  int ret = ::fcntl(sockfd, F_SETFL, flags);
  // FIXME check

  // close-on-exec
  flags = ::fcntl(sockfd, F_GETFD, 0);
  flags |= FD_CLOEXEC;
  ret = ::fcntl(sockfd, F_SETFD, flags);
  // FIXME check

  (void)ret;
}
#endif

}  // namespace

/*��ʽת��������types.h�ж���
template<typename To, typename From>
inline To implicit_cast(From const &f)
{
  return f;
}
*/

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in6* addr)
{
  return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));  // implicit_cast��ʽת�� ����c���������͸�ֵ��static_cast������c��ǿ������ת��
}

struct sockaddr* sockets::sockaddr_cast(struct sockaddr_in6* addr)
{
  return static_cast<struct sockaddr*>(implicit_cast<void*>(addr));
}

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in* addr)
{
  return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));
}

const struct sockaddr_in* sockets::sockaddr_in_cast(const struct sockaddr* addr)
{
  return static_cast<const struct sockaddr_in*>(implicit_cast<const void*>(addr));
}

const struct sockaddr_in6* sockets::sockaddr_in6_cast(const struct sockaddr* addr)
{
  return static_cast<const struct sockaddr_in6*>(implicit_cast<const void*>(addr));
}


/*
int socket(int domain, int type, int protocol);
����ֵ��
  �ɹ�������ָ���´�����socket���ļ���������ʧ�ܣ�����-1������errno

�����ǵ���socket����һ��socketʱ�����ص�socket��������������Э���壨address family��AF_XXX���ռ��У�
��û��һ������ĵ�ַ�������Ҫ������ֵһ����ַ���ͱ������bind()������
����͵�����connect()��listen()ʱϵͳ���Զ��������һ���˿ڡ�
*/
int sockets::createNonblockingOrDie(sa_family_t family)
{
#if VALGRIND
  int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0)
  {
    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
  }

  setNonBlockAndCloseOnExec(sockfd);
#else
  int sockfd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
  if (sockfd < 0)
  {
    LOG_SYSFATAL << "sockets::createNonblockingOrDie";
  }
#endif
  return sockfd;
}

/*
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
��һ����ַ���е��ض���ַ����socket��
*/
void sockets::bindOrDie(int sockfd, const struct sockaddr* addr)
{
  int ret = ::bind(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
  if (ret < 0)
  {
    LOG_SYSFATAL << "sockets::bindOrDie";
  }
}


/*
int listen(int sockfd, int backlog);
  backlog:�Ŷӽ���3�����ֶ��к͸ոս���3�����ֶ��е���������
          �鿴ϵͳĬ��backlog:
          cat /proc/sys/net/ipv4/tcp_max_syn_backlog

  ������ͬʱ�����ڶ���ͻ��ˣ����пͻ��˷�������ʱ��
  ���������õ�accept()���ز�����������ӣ�����д����Ŀͻ��˷������Ӷ�����������������
  ��δaccept�Ŀͻ��˾ʹ������ӵȴ�״̬��listen()����sockfd���ڼ���״̬��
  �������������backlog���ͻ��˴������Ӵ�״̬��
  ������յ��������������ͺ��ԡ�listen()�ɹ�����0��ʧ�ܷ���-1��
*/
void sockets::listenOrDie(int sockfd)
{
  //�ڶ���������ȫ���Ӷ��еĸ�����ʵ��ȫ���Ӹ���=�˲���+1
  //SOMAXCONN������ϵͳ��ÿһ���˿����ļ������еĳ���,���Ǹ�ȫ�ֵĲ���,Ĭ��ֵΪ1024
  int ret = ::listen(sockfd, SOMAXCONN);
  if (ret < 0)
  {
    LOG_SYSFATAL << "sockets::listenOrDie";
  }
}

/*
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
����ͻ������������󣬱���ʹ��accept���������ܿͻ��˵�����
  s�а�����������ip��port
  addr�Ǵ����������������ӿͻ��˵�ַ��Ϣ����IP��ַ�Ͷ˿ں�

�ɹ�����һ���µ�socket�ļ�������(new_socket)�����ںͿͻ���ͨ�ţ�ʧ�ܷ���-1������errno
֮���send��recv����ָ�����new_socket
*/
int sockets::accept(int sockfd, struct sockaddr_in6* addr)
{
  //�κκ���Ŀⶼ���뱣֤ socklen_t �� int ����ͬ�ĳ���
  socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
#if VALGRIND || defined (NO_ACCEPT4)
  int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
  setNonBlockAndCloseOnExec(connfd);
#else
  int connfd = ::accept4(sockfd, sockaddr_cast(addr),
                         &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
  //accept4()�ǷǱ�׼Linux��չ
#endif
  if (connfd < 0)
  {
    int savedErrno = errno;
    LOG_SYSERR << "Socket::accept";
    switch (savedErrno)
    {
      case EAGAIN:
      case ECONNABORTED:
      case EINTR:
      case EPROTO: // ???
      case EPERM:
      case EMFILE: // per-process lmit of open file desctiptor ???
        // expected errors
        errno = savedErrno;
        break;
      case EBADF:
      case EFAULT:
      case EINVAL:
      case ENFILE:
      case ENOBUFS:
      case ENOMEM:
      case ENOTSOCK:
      case EOPNOTSUPP:
        // unexpected errors
        LOG_FATAL << "unexpected error of ::accept " << savedErrno;
        break;
      default:
        LOG_FATAL << "unknown error of ::accept " << savedErrno;
        break;
    }
  }
  return connfd;
}


//�ͻ��˵���
//addr�����������ָ���������˵�ַ��Ϣ����IP��ַ�Ͷ˿ں�
int sockets::connect(int sockfd, const struct sockaddr* addr)
{
  return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
}

/*
����I/O���������漸�飺
read()/write()
recv()/send()
readv()/writev()
recvmsg()/sendmsg()
recvfrom()/sendto()

readv��read�Ĳ�֮ͬ�������յ����ݿ�����䵽���������

*/
ssize_t sockets::read(int sockfd, void *buf, size_t count)
{
  return ::read(sockfd, buf, count);
}

ssize_t sockets::readv(int sockfd, const struct iovec *iov, int iovcnt)
{
  return ::readv(sockfd, iov, iovcnt);
}

ssize_t sockets::write(int sockfd, const void *buf, size_t count)
{
  return ::write(sockfd, buf, count);
}

void sockets::close(int sockfd)
{
  if (::close(sockfd) < 0)
  {
    LOG_SYSERR << "sockets::close";
  }
}

void sockets::shutdownWrite(int sockfd)
{
  if (::shutdown(sockfd, SHUT_WR) < 0)  // ���ŵĹر��׽���
      // ������writeʱ����ʱ�ǽ�����д������д�����������˿������Ҫ�ر��׽��֣���֪����д���塱�������Ƿ��Ѿ����ͳ�ȥ���˿̾Ϳ��Ե���shutdown�����ŵĹر�
  {
    LOG_SYSERR << "sockets::shutdownWrite";
  }
}

/// SHUT_RD���Ͽ����������׽����޷��������ݣ���ʹ���뻺�����յ�����Ҳ��Ĩȥ�����޷�����������غ�����
/// SHUT_WR���Ͽ���������׽����޷��������ݣ����������������л���δ��������ݣ��򽫴��ݵ�Ŀ��������
/// SHUT_RDWR��ͬʱ�Ͽ� I/O �����൱�ڷ����ε��� shutdown()������һ���� SHUT_RD Ϊ��������һ���� SHUT_WR Ϊ����


//����ַת��Ϊip��port���ַ�������buf�У�buf��ŵ�IP��ַ�ǵ��ʮ����
void sockets::toIpPort(char* buf, size_t size,
                       const struct sockaddr* addr)
{
  if (addr->sa_family == AF_INET6)
  {
    buf[0] = '[';
    toIp(buf+1, size-1, addr);
    size_t end = ::strlen(buf);
    const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
    uint16_t port = sockets::networkToHost16(addr6->sin6_port);
    assert(size > end);
    snprintf(buf+end, size-end, "]:%u", port);
    return;
  }
  toIp(buf, size, addr);
  size_t end = ::strlen(buf);
  const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
  uint16_t port = sockets::networkToHost16(addr4->sin_port);
  assert(size > end);
  snprintf(buf+end, size-end, ":%u", port);
}


/*
inet_aton() ת������������ַip(��192.168.1.10)Ϊ��������ֵ,
���ת��������������紫�䣬����Ҫ����htons��htonl�������ܽ������ֽ�˳��ת��Ϊ�����ֽ�˳��

inet_addr����ת������������ַ����192.168.1.10)Ϊ�����ֽ��������ֵ,
255.255.255.255��һ����Ч�ĵ�ַ������inet_addr�޷�����;

inet_ntoa ����ת�������ֽ�����ĵ�ַΪ��׼��ASCII�Ե�ֿ��ĵ�ַ,
�ú�������ָ���ֿ����ַ�����ַ����192.168.1.10)��ָ�룬
���ַ����Ŀռ�Ϊ��̬����ģ�����ζ���ڵڶ��ε��øú���ʱ��
��һ�ε��ý��ᱻ��д�����ǣ������������Ҫ����ô�����Ƴ����Լ�����


��IPv6���ֵĺ���
#include <arpe/inet.h>
int inet_pton(int family, const char *strptr, void *addrptr);
//�����ʮ���Ƶ�ip��ַת��Ϊ�������紫�����ֵ��ʽ
        ����ֵ�����ɹ���Ϊ1�������벻����Ч�ı��ʽ��Ϊ0����������Ϊ-1

const char * inet_ntop(int family, const void *addrptr, char *strptr, size_t len);
//����ֵ��ʽת��Ϊ���ʮ���Ƶ�ip��ַ��ʽ
        ����ֵ�����ɹ���Ϊָ��ṹ��ָ�룬��������ΪNULL

p---presentation
n---numeric
*/
void sockets::toIp(char* buf, size_t size,
                   const struct sockaddr* addr)
{
  if (addr->sa_family == AF_INET)
  {
    assert(size >= INET_ADDRSTRLEN);
    const struct sockaddr_in* addr4 = sockaddr_in_cast(addr);
    ::inet_ntop(AF_INET, &addr4->sin_addr, buf, static_cast<socklen_t>(size));  // inet_ntop��һ��IP��ַת�������������������ֽ����IP��ַת��Ϊ����ı���IP��ַ
  }
  else if (addr->sa_family == AF_INET6)
  {
    assert(size >= INET6_ADDRSTRLEN);
    const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
    ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, static_cast<socklen_t>(size));
  }
}

//��ip��portת��sockadd_in����
void sockets::fromIpPort(const char* ip, uint16_t port,
                         struct sockaddr_in* addr)
{
  addr->sin_family = AF_INET;
  addr->sin_port = hostToNetwork16(port);
  if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
  {
    LOG_SYSERR << "sockets::fromIpPort";
  }
}

void sockets::fromIpPort(const char* ip, uint16_t port,
                         struct sockaddr_in6* addr)
{
  addr->sin6_family = AF_INET6;
  addr->sin6_port = hostToNetwork16(port);
  if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0)
  {
    LOG_SYSERR << "sockets::fromIpPort";
  }
}

//����socket����
int sockets::getSocketError(int sockfd)
{
  int optval;
  socklen_t optlen = static_cast<socklen_t>(sizeof optval);
  //���socket��һЩѡ��
  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
  {
    return errno;
  }
  else
  {
    return optval;
  }
}

//��ȡ��ĳ���׽��ֹ����ı���Э���ַ,getsockname
//���������δ����bind()�͵�����connect()��һ�����connect()�Ķ��ǿͻ��ˣ����Ͳ���Ҫbind()
//��ʱΨ��getsockname()���ÿ��Ի�֪���ں˸�������ӵı���IP��ַ�ͱ��ض˿ںš�
struct sockaddr_in6 sockets::getLocalAddr(int sockfd)
{
  struct sockaddr_in6 localaddr;
  memZero(&localaddr, sizeof localaddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
  if (::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
  {
    LOG_SYSERR << "sockets::getLocalAddr";
  }
  return localaddr;
}

//��ȡ��ĳ���׽��ֹ��������Э���ַ
//��TCP�ķ�������accept�ɹ���
//ͨ��getpeername()��������ȡ��ǰ���ӵĿͻ��˵�IP��ַ�Ͷ˿ںš�
struct sockaddr_in6 sockets::getPeerAddr(int sockfd)
{
  struct sockaddr_in6 peeraddr;
  memZero(&peeraddr, sizeof peeraddr);
  socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
  if (::getpeername(sockfd, sockaddr_cast(&peeraddr), &addrlen) < 0)
  {
    LOG_SYSERR << "sockets::getPeerAddr";
  }
  return peeraddr;
}

//����getpeername��getsockname�жϵ�ǰ���ӵĿͻ����ǲ����Լ���Ҳ�����Լ����Լ�
bool sockets::isSelfConnect(int sockfd)
{
  struct sockaddr_in6 localaddr = getLocalAddr(sockfd);
  struct sockaddr_in6 peeraddr = getPeerAddr(sockfd);
  if (localaddr.sin6_family == AF_INET)
  {
    const struct sockaddr_in* laddr4 = reinterpret_cast<struct sockaddr_in*>(&localaddr);
    const struct sockaddr_in* raddr4 = reinterpret_cast<struct sockaddr_in*>(&peeraddr);
    return laddr4->sin_port == raddr4->sin_port
        && laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
  }
  else if (localaddr.sin6_family == AF_INET6)
  {
    return localaddr.sin6_port == peeraddr.sin6_port
        && memcmp(&localaddr.sin6_addr, &peeraddr.sin6_addr, sizeof localaddr.sin6_addr) == 0;
  }
  else
  {
    return false;
  }
}

