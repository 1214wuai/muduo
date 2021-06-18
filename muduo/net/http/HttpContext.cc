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
  while (hasMore)  // 解析请求包状态机，这么做的原因在于buf中可能不是完整的请求包，所以分段来解析
  {
    if (state_ == kExpectRequestLine)  // 当前需要解析请求行
    {
      const char* crlf = buf->findCRLF();
      if (crlf)
      {
        ok = processRequestLine(buf->peek(), crlf);  // 返回解析请求行解析结果
        if (ok)  // 请求行解析成功则继续
        {
          request_.setReceiveTime(receiveTime);
          buf->retrieveUntil(crlf + 2);  // 移动缓存中的读指针
          state_ = kExpectHeaders;  // 下一步解析请求头
        }
        else  // 解析请求行失败，直接退出状态机
        {
          hasMore = false;
        }
      }
      else
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectHeaders)  // 解析请求头
    {
      const char* crlf = buf->findCRLF();  // 找到 "\r\n" 所在位置
      if (crlf)  // 没有找到 '\r\n',直接退出状态机
      {
        const char* colon = std::find(buf->peek(), crlf, ':');
        if (colon != crlf)
        {
          request_.addHeader(buf->peek(), colon, crlf);
        }
        else  // todo starsli 这里的判断空行并不标准，需要验证一下，这里居然以没有发现':'就当成空行
        {
          // empty line, end of header
          // FIXME:
          state_ = kGotAll;  // 遇到空行，准备下一步解析，剩下的全都是响应体
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
