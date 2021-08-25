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
struct iovec������һ������Ԫ�ء�ͨ��������ṹ����һ����Ԫ�ص����顣
����ÿһ�������Ԫ�أ�ָ���Աiov_baseָ��һ��������������������Ǵ�ŵ���readv�����յ����ݻ���writev��Ҫ���͵����ݡ�
��Աiov_len�ڸ�������·ֱ�ȷ���˽��յ���󳤶��Լ�ʵ��д��ĳ��ȡ���iovec�ṹ������scatter/gather IO�ġ�
readv��writev����������һ�κ��������ж���д�������������������ʱҲ��������������Ϊɢ������scatter read���;ۼ�д��gather write����

��ɢ��scatter����Channel�ж�ȡ��ָ�ڶ�����ʱ����ȡ������д����buffer�С���ˣ�Channel����Channel�ж�ȡ�����ݡ���ɢ��scatter���������Buffer�С�
�ۼ���gather��д��Channel��ָ��д����ʱ�����buffer������д��ͬһ��Channel����ˣ�Channel �����Buffer�е����ݡ��ۼ���gather�������͵�Channel

#include <sys/uio.h>
 // Structure for scatter/gather I/O.

struct iovec {
    ptr_t iov_base; //Starting address
    size_t iov_len; //Length in bytes
};


ssize_t readv(int fd, const struct iovec *iov, int iovcnt);

ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

    ������������Ҫ����������

      Ҫ�����Ͻ��ж�����д���ļ�������fd
      ����д���õ�I/O����(vector)
      Ҫʹ�õ�����Ԫ�ظ���(count)

*/
//Buffer�ĺ��ĺ���֮һ
//���ջ�Ͽռ䣬�����ڴ�ʹ�ù�������ڴ�ʹ����
//�����10K�����ӣ�ÿ�����Ӿͷ���64K�������Ļ�����ռ��640M�ڴ�
//�������ʱ����Щ��������ʹ���ʺܵ�


//�ú����������׽����򻺳�����ȡ����
ssize_t Buffer::readFd(int fd, int* savedErrno)
{
  // saved an ioctl()/FIONREAD call to tell how much to read

  //��ʡһ��ioctlϵͳ����(��ȡ��ǰ�ж��ٿɶ����ݣ�
  //Ϊʲô��ô˵?��Ϊ����׼�����㹻���extrabuf����ô���ǾͲ���Ҫʹ��ioctlȥ�鿴fd�ж��ٿɶ��ֽ�����
  char extrabuf[65536];
  struct iovec vec[2];                                                                      //ʹ��iovec�������������Ļ�����
  const size_t writable = writableBytes();                                                  //buffer�л�ʣ���ٿռ����д
  vec[0].iov_base = begin()+writerIndex_;                                                   //��һ�黺������ָ���д�ռ�
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;                                                               //�ڶ��黺������ָ��ջ�Ͽռ�
  vec[1].iov_len = sizeof extrabuf;
  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;                                  //writeableһ��С��65536
  const ssize_t n = sockets::readv(fd, vec, iovcnt);                                        //iovcnt=2
  if (n < 0)                                                                                //�д���������-1
  {
    *savedErrno = errno;
  }
  else if (implicit_cast<size_t>(n) <= writable)                                            //��һ�黺�����㹻����
  {
    writerIndex_ += n;                                                                      //ֱ�Ӽ�n
  }
  else                                                                                      //��ǰ���������������ɣ�������ݱ����ܵ��˵ڶ��黺����extrabuf������append��buffer
  {
    writerIndex_ = buffer_.size();                                                          //�ȸ��µ�ǰwriterIndex����ʱ��buffer�Ѿ�д���ˣ���ʣ��n-writable��������extrabuf��
    append(extrabuf, n - writable);                                                         //Ȼ��׷��ʣ��Ľ���buffer����
  }
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  return n;
}

