// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Buffer.h"

#include "muduo/net/SocketsOps.h"

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

/*
struct iovec定义了一个向量元素。通常，这个结构用作一个多元素的数组。
对于每一个传输的元素，指针成员iov_base指向一个缓冲区，这个缓冲区是存放的是readv所接收的数据或是writev将要发送的数据。
成员iov_len在各种情况下分别确定了接收的最大长度以及实际写入的长度。且iovec结构是用于scatter/gather IO的。
readv和writev函数用于在一次函数调用中读、写多个非连续缓冲区。有时也将这两个函数称为散布读（scatter read）和聚集写（gather write）。

分散（scatter）从Channel中读取是指在读操作时将读取的数据写入多个buffer中。因此，Channel将从Channel中读取的数据“分散（scatter）”到多个Buffer中。
聚集（gather）写入Channel是指在写操作时将多个buffer的数据写入同一个Channel，因此，Channel 将多个Buffer中的数据“聚集（gather）”后发送到Channel

#include <sys/uio.h>
 // Structure for scatter/gather I/O.

struct iovec {
    ptr_t iov_base; //Starting address
    size_t iov_len; //Length in bytes
};


ssize_t readv(int fd, const struct iovec *iov, int iovcnt);

ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

    这两个函数需要三个参数：

      要在其上进行读或是写的文件描述符fd
      读或写所用的I/O向量(vector)
      要使用的向量元素个数(count)

*/
//Buffer的核心函数之一
//结合栈上空间，避免内存使用过大，提高内存使用率
//如果有10K个连接，每个连接就分配64K缓冲区的话，将占用640M内存
//而大多数时候，这些缓冲区的使用率很低


//该函数用来从套接字向缓冲区读取数据
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
  // saved an ioctl()/FIONREAD call to tell how much to read

  //节省一次ioctl系统调用(获取当前有多少可读数据）
  //为什么这么说?因为我们准备了足够大的extrabuf，那么我们就不需要使用ioctl去查看fd有多少可读字节数了
  char extrabuf[65536];
  struct iovec vec[2];                                                                      //使用iovec分配两个连续的缓冲区
  const size_t writable = writableBytes();                                                  //buffer中还剩多少空间可以写
  vec[0].iov_base = begin()+writerIndex_;                                                   //第一块缓冲区，指向可写空间
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;                                                               //第二块缓冲区，指向栈上空间
  vec[1].iov_len = sizeof extrabuf;
  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;                                  //writeable一般小于65536
  const ssize_t n = sockets::readv(fd, vec, iovcnt);                                        //iovcnt=2
  if (n < 0)                                                                                //有错误发生返回-1
  {
    *savedErrno = errno;
  }
  else if (implicit_cast<size_t>(n) <= writable)                                            //第一块缓冲区足够容纳
  {
    writerIndex_ += n;                                                                      //直接加n
  }
  else                                                                                      //当前缓冲区，不够容纳，因而数据被接受到了第二块缓冲区extrabuf，将其append至buffer
  {
    writerIndex_ = buffer_.size();                                                          //先更新当前writerIndex，此时的buffer已经写满了，还剩下n-writable的数据在extrabuf中
    append(extrabuf, n - writable);                                                         //然后追加剩余的进入buffer当中
  }
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  return n;
}

