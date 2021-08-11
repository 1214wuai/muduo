// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/Timestamp.h"

#include <sys/time.h>
#include <stdio.h>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

using namespace muduo;

static_assert(sizeof(Timestamp) == sizeof(int64_t),
              "Timestamp should be same size as int64_t");

string Timestamp::toString() const
{
  char buf[32] = {0};
  int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
  int64_t microseconds = microSecondsSinceEpoch_ % kMicroSecondsPerSecond;
  snprintf(buf, sizeof(buf), "%" PRId64 ".%06" PRId64 "", seconds, microseconds);
  return buf;
}
/*
C++ʹ��PRID64��Ҫ������
����ͷ�ļ���<inttypes.h>
����꣺__STDC_FORMAT_MACROS������ͨ������ʱ��-D__STDC_FORMAT_MACROS�������ڰ����ļ�֮ǰ��������ꡣ
int64_t������ʾ64λ��������32λϵͳ����long long int����64λϵͳ����long int,���Դ�ӡint64_t�ĸ�ʽ�������ǣ�
printf(��%ld��,value);//64bitOS
printf("%lld",value);//32bitOS
��ƽ̨��������
#define__STDC_FORMAT_MACROS
#include<inttypes.h>
#undef__STDC_FORMAT_MACROS
printf("%"PRId64"\n",value);
*/
//PRId64���ڿ�ƽ̨�ģ�from��<inttypes.h>������64bit�ģ��͵���ld������32bit�ģ��͵���lld



/*
struct tm *gmtime(consttime_t*timep);
struct tm *gmtime_r(consttime_t*timep,structtm*result);
����ʱ����Ǵ�1970.1.1�����ڵ�����
gmtime()����������ʱ��timepת��Ϊ��UTCʱ���ʾ��ʱ�䡣�����ܷ���NULL��������ݲ��ܷŵ�һ�������С�
����ֵָ��һ����̬����Ľṹ���ýṹ���ܻᱻ���������κ����ں�ʱ�亯�����ø��ǡ�
gmtime_r()�������������ͬ�����������Խ����ݴ洢���û��ṩ�Ľṹ���С�
*/
string Timestamp::toFormattedString(bool showMicroseconds) const
{
  char buf[64] = {0};
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch_ / kMicroSecondsPerSecond);
  struct tm tm_time;
  gmtime_r(&seconds, &tm_time);

  if (showMicroseconds)
  {
    int microseconds = static_cast<int>(microSecondsSinceEpoch_ % kMicroSecondsPerSecond);
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d.%06d",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
             microseconds);
  }
  else
  {
    snprintf(buf, sizeof(buf), "%4d%02d%02d %02d:%02d:%02d",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
  }
  return buf;
}

//��ȡ��ǰʱ��
Timestamp Timestamp::now()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);//gettimeofday(,ʱ��)��NULL��ʾû��ʱ��
  int64_t seconds = tv.tv_sec;//��ʾtv.tv_sec��
  return Timestamp(seconds * kMicroSecondsPerSecond + tv.tv_usec);//tv.tv_usec��ʾ΢��}
}
//1us=�����֮1s��us/1000000����λ��Ϊ��us��
//΢�롣1us=1/1000000sms��
//���롣1ms=1/1000s��Ĭ��ֵs��
//�롣1s=1000ms
