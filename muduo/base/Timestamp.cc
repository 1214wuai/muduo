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
C++使用PRID64，要两步：
包含头文件：<inttypes.h>
定义宏：__STDC_FORMAT_MACROS，可以通过编译时加-D__STDC_FORMAT_MACROS，或者在包含文件之前定义这个宏。
int64_t用来表示64位整数，在32位系统中是long long int，在64位系统中是long int,所以打印int64_t的格式化方法是：
printf(“%ld”,value);//64bitOS
printf("%lld",value);//32bitOS
跨平台的做法：
#define__STDC_FORMAT_MACROS
#include<inttypes.h>
#undef__STDC_FORMAT_MACROS
printf("%"PRId64"\n",value);
*/
//PRId64用于跨平台的，from：<inttypes.h>，若是64bit的，就等于ld，若是32bit的，就等于lld



/*
struct tm *gmtime(consttime_t*timep);
struct tm *gmtime_r(consttime_t*timep,structtm*result);
日历时间就是从1970.1.1到现在的秒数
gmtime()函数将日历时间timep转换为用UTC时间表示的时间。它可能返回NULL，比如年份不能放到一个整数中。
返回值指向一个静态分配的结构，该结构可能会被接下来的任何日期和时间函数调用覆盖。
gmtime_r()函数功能与此相同，但是它可以将数据存储到用户提供的结构体中。
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

//获取当前时间
Timestamp Timestamp::now()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);//gettimeofday(,时区)，NULL表示没有时区
  int64_t seconds = tv.tv_sec;//表示tv.tv_sec秒
  return Timestamp(seconds * kMicroSecondsPerSecond + tv.tv_usec);//tv.tv_usec表示微妙}
}
//1us=百万分之1s，us/1000000，单位成为秒us：
//微秒。1us=1/1000000sms：
//毫秒。1ms=1/1000s。默认值s：
//秒。1s=1000ms
