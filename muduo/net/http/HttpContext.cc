// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include "muduo/net/Buffer.h"
#include "muduo/net/http/HttpContext.h"

using namespace muduo;
using namespace muduo::net;

bool HttpContext::processRequestLine(const char* begin, const char* end)
{
  bool succeed = false;
  const char* start = begin;
  const char* space = std::find(start, end, ' ');
  if (space != end && request_.setMethod(start, space))
  {
    start = space+1;
    space = std::find(start, end, ' ');
    if (space != end)
    {
      const char* question = std::find(start, space, '?');
      if (question != space)
      {
        request_.setPath(start, question);
        request_.setQuery(question, space);
      }
      else
      {
        request_.setPath(start, space);
      }
      start = space+1;
      succeed = end-start == 8 && std::equal(start, end-1, "HTTP/1.");
      if (succeed)
      {
        if (*(end-1) == '1')
        {
          request_.setVersion(HttpRequest::kHttp11);
        }
        else if (*(end-1) == '0')
        {
          request_.setVersion(HttpRequest::kHttp10);
        }
        else
        {
          succeed = false;
        }
      }
    }
  }
  return succeed;
}

// return false if any error
bool HttpContext::parseRequest(Buffer* buf, Timestamp receiveTime)
{
  bool ok = true;
  bool hasMore = true;
  while (hasMore)  // ���������״̬������ô����ԭ������buf�п��ܲ�������������������Էֶ�������
  {
    if (state_ == kExpectRequestLine)  // ��ǰ��Ҫ����������
    {
      const char* crlf = buf->findCRLF();
      if (crlf)
      {
        ok = processRequestLine(buf->peek(), crlf);  // ���ؽ��������н������
        if (ok)  // �����н����ɹ������
        {
          request_.setReceiveTime(receiveTime);
          buf->retrieveUntil(crlf + 2);  // �ƶ������еĶ�ָ��
          state_ = kExpectHeaders;  // ��һ����������ͷ
        }
        else  // ����������ʧ�ܣ�ֱ���˳�״̬��
        {
          hasMore = false;
        }
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectHeaders)  // ��������ͷ
    {
      const char* crlf = buf->findCRLF();  // �ҵ� "\r\n" ����λ��
      if (crlf)  // û���ҵ� '\r\n',ֱ���˳�״̬��
      {
        const char* colon = std::find(buf->peek(), crlf, ':');
        if (colon != crlf)
        {
          request_.addHeader(buf->peek(), colon, crlf);
        }
        else  // todo starsli ������жϿ��в�����׼����Ҫ��֤һ�£������Ȼ��û�з���':'�͵��ɿ���
        {
          // empty line, end of header
          // FIXME:
          state_ = kGotAll;  // �������У�׼����һ��������ʣ�µ�ȫ������Ӧ��
          hasMore = false;
        }
        buf->retrieveUntil(crlf + 2);
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectBody)
    {
      // FIXME:
    }
  }
  return ok;
}
