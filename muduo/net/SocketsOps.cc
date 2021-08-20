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

//fcntl函数，是把套接字设置为非阻塞式I/O型或者信号驱动式I/O型以及设置套接字属主的POSIX的方式
//此处设为非阻塞和closeexec

/*
clsoe on exec
关闭子进程无用文件描述符
当fork子进程后，仍然可以使用fd。但执行exec后系统就会字段关闭子进程中的fd了
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

/*隐式转换函数，types.h中定义
template<typename To, typename From>
inline To implicit_cast(From const &f)
{
  return f;
}
*/

const struct sockaddr* sockets::sockaddr_cast(const struct sockaddr_in6* addr)
{
  return static_cast<const struct sockaddr*>(implicit_cast<const void*>(addr));  // implicit_cast隐式转换 类似c的内置类型赋值，static_cast类似于c的强制类型转换
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
返回值：
  成功：返回指向新创建的socket的文件描述符，失败：返回-1，设置errno

当我们调用socket创建一个socket时，返回的socket描述字它存在于协议族（address family，AF_XXX）空间中，
但没有一个具体的地址。如果想要给它赋值一个地址，就必须调用bind()函数，
否则就当调用connect()、listen()时系统会自动随机分配一个端口。
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
把一个地址族中的特定地址赋给socket。
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
  backlog:排队建立3次握手队列和刚刚建立3次握手队列的链接数和
          查看系统默认backlog:
          cat /proc/sys/net/ipv4/tcp_max_syn_backlog

  服务器同时服务于多个客户端，当有客户端发起连接时，
  服务器调用的accept()返回并接受这个连接，如果有大量的客户端发起连接而服务器来不及处理，
  尚未accept的客户端就处于连接等待状态，listen()声明sockfd处于监听状态，
  并且最多允许有backlog个客户端处于连接待状态，
  如果接收到更多的连接请求就忽略。listen()成功返回0，失败返回-1。
*/
void sockets::listenOrDie(int sockfd)
{
  //第二个参数：全链接队列的个数，实际全连接个数=此参数+1
  //SOMAXCONN定义了系统中每一个端口最大的监听队列的长度,这是个全局的参数,默认值为1024
  int ret = ::listen(sockfd, SOMAXCONN);
  if (ret < 0)
  {
    LOG_SYSFATAL << "sockets::listenOrDie";
  }
}

/*
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
如果客户端有连接请求，必须使用accept函数来接受客户端的请求。
  s中包含服务器的ip和port
  addr是传出参数，返回链接客户端地址信息，含IP地址和端口号

成功返回一个新的socket文件描述符(new_socket)，用于和客户端通信，失败返回-1，设置errno
之后的send和recv都是指向这个new_socket
*/
int sockets::accept(int sockfd, struct sockaddr_in6* addr)
{
  //任何合理的库都必须保证 socklen_t 与 int 有相同的长度
  socklen_t addrlen = static_cast<socklen_t>(sizeof *addr);
#if VALGRIND || defined (NO_ACCEPT4)
  int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
  setNonBlockAndCloseOnExec(connfd);
#else
  int connfd = ::accept4(sockfd, sockaddr_cast(addr),
                         &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
  //accept4()是非标准Linux扩展
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


//客户端调用
//addr：传入参数，指定服务器端地址信息，含IP地址和端口号
int sockets::connect(int sockfd, const struct sockaddr* addr)
{
  return ::connect(sockfd, addr, static_cast<socklen_t>(sizeof(struct sockaddr_in6)));
}

/*
网络I/O操作有下面几组：
read()/write()
recv()/send()
readv()/writev()
recvmsg()/sendmsg()
recvfrom()/sendto()

readv和read的不同之处，接收的数据可以填充到多个缓冲区

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
  if (::shutdown(sockfd, SHUT_WR) < 0)  // 优雅的关闭套接字
      // 当调用write时，此时是将数据写入了向“写缓冲区”，此刻如果需要关闭套接字，不知道“写缓冲”中数据是否已经发送出去，此刻就可以调用shutdown，优雅的关闭
  {
    LOG_SYSERR << "sockets::shutdownWrite";
  }
}

/// SHUT_RD：断开输入流。套接字无法接收数据（即使输入缓冲区收到数据也被抹去），无法调用输入相关函数。
/// SHUT_WR：断开输出流。套接字无法发送数据，但如果输出缓冲区中还有未传输的数据，则将传递到目标主机。
/// SHUT_RDWR：同时断开 I/O 流。相当于分两次调用 shutdown()，其中一次以 SHUT_RD 为参数，另一次以 SHUT_WR 为参数


//将地址转化为ip和port的字符串放在buf中，buf存放的IP地址是点分十进制
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
inet_aton() 转换网络主机地址ip(如192.168.1.10)为二进制数值,
这个转换完后不能用于网络传输，还需要调用htons或htonl函数才能将主机字节顺序转化为网络字节顺序

inet_addr函数转换网络主机地址（如192.168.1.10)为网络字节序二进制值,
255.255.255.255是一个有效的地址，不过inet_addr无法处理;

inet_ntoa 函数转换网络字节排序的地址为标准的ASCII以点分开的地址,
该函数返回指向点分开的字符串地址（如192.168.1.10)的指针，
该字符串的空间为静态分配的，这意味着在第二次调用该函数时，
上一次调用将会被重写（复盖），所以如果需要保存该串最后复制出来自己管理！


随IPv6出现的函数
#include <arpe/inet.h>
int inet_pton(int family, const char *strptr, void *addrptr);
//将点分十进制的ip地址转化为用于网络传输的数值格式
        返回值：若成功则为1，若输入不是有效的表达式则为0，若出错则为-1

const char * inet_ntop(int family, const void *addrptr, char *strptr, size_t len);
//将数值格式转化为点分十进制的ip地址格式
        返回值：若成功则为指向结构的指针，若出错则为NULL

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
    ::inet_ntop(AF_INET, &addr4->sin_addr, buf, static_cast<socklen_t>(size));  // inet_ntop是一个IP地址转换函数，二进制网络字节序的IP地址转换为点分文本的IP地址
  }
  else if (addr->sa_family == AF_INET6)
  {
    assert(size >= INET6_ADDRSTRLEN);
    const struct sockaddr_in6* addr6 = sockaddr_in6_cast(addr);
    ::inet_ntop(AF_INET6, &addr6->sin6_addr, buf, static_cast<socklen_t>(size));
  }
}

//从ip和port转化sockadd_in类型
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

//返回socket错误
int sockets::getSocketError(int sockfd)
{
  int optval;
  socklen_t optlen = static_cast<socklen_t>(sizeof optval);
  //获得socket的一些选项
  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
  {
    return errno;
  }
  else
  {
    return optval;
  }
}

//获取与某个套接字关联的本地协议地址,getsockname
//适用情况：未调用bind()就调用了connect()，一般调用connect()的都是客户端，本就不需要bind()
//这时唯有getsockname()调用可以获知由内核赋予该连接的本地IP地址和本地端口号。
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

//获取与某个套接字关联的外地协议地址
//在TCP的服务器端accept成功后，
//通过getpeername()函数来获取当前连接的客户端的IP地址和端口号。
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

//利用getpeername和getsockname判断当前连接的客户端是不是自己，也就是自己连自己
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

