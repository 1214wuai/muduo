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

/*
���󷽷�     ����URL   HTTPЭ��İ汾
*/
bool HttpContext::processRequestLine(const char* begin, const char* end)               //���������У����У�,begin�������е��׵�ַ��end�������е�β��ַ��end�ڸú�����һֱû�б��
{
  bool succeed = false;
  const char* start = begin;
  const char* space = std::find(start, end, ' ');                                      //�ҵ����󷽷���spaceָ���һ���ո�
  if (space != end && request_.setMethod(start, space))                                //������󷽷�
  {
    start = space+1;                                                                   //����start��ʹ��ָ������URL���׵�ַ
    space = std::find(start, end, ' ');                                                //�ҵ�����URL��spaceָ��ڶ����ո�
    if (space != end)
    {
      const char* question = std::find(start, space, '?');                             //URL�ʺ�ǰ���Ƿ��ʵ���Դ�������Ǵ���Ĳ���
      if (question != space)
      {
        request_.setPath(start, question);                                             //���ʵ���Դ������Э�鷽��������¼��Ϣ����������ַ(���� ip)���������˿ڣ�����ε��ļ�·��(web������Ŀ¼)
        request_.setQuery(question, space);
      }
      else
      {
        request_.setPath(start, space);                                                //URL�ʺ���URL����ĩβ����ʾû�д������
      }
      start = space+1;
      succeed = end-start == 8 && std::equal(start, end-1, "HTTP/1.");                 //�ȽϺ�����ֻ�Ƚ�ǰ�������������ķ�Χ[start,end-1],ǰ�պ��
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
    if (state_ == kExpectRequestLine)                                                        //��ǰ��Ҫ����������
    {
      const char* crlf = buf->findCRLF();
      if (crlf)
      {
        ok = processRequestLine(buf->peek(), crlf);                                          //���ؽ��������н������
        if (ok)                                                                              //�����н����ɹ������
        {
          request_.setReceiveTime(receiveTime);
          buf->retrieveUntil(crlf + 2);                                                      //�ƶ������еĶ�ָ�룬��ǰ���Ѿ�����������+���з�ȫ������
          state_ = kExpectHeaders;                                                           //��һ����������ͷ����һ�ν���whileѭ����ʱ��
        }
        else                                                                                 //����������ʧ�ܣ�ֱ���˳�״̬��
        {
          hasMore = false;
        }
      }
      else                                                                                   //û���ҵ����е�\r\n��ֱ���˳�״̬��
      {
        hasMore = false;
      }
    }
    else if (state_ == kExpectHeaders)  //��������ͷ
    {
      const char* crlf = buf->findCRLF();  // �ҵ� "\r\n" ����λ��
      if (crlf)
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
        buf->retrieveUntil(crlf + 2);                                                         //ÿ�δ���һ������ͷ���ߴ������֮�󶼻������ƶ���ָ�룬�ѸղŴ������һ�б�Ϊ���ɶ�
      }
      else                                                                                    //û���ҵ� '\r\n',ֱ���˳�״̬��
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
