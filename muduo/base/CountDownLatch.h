// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"

namespace muduo
{

class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);

  void wait();

  void countDown();

  int getCount() const;

 private:
  mutable MutexLock mutex_;
  Condition condition_ GUARDED_BY(mutex_);
  int count_ GUARDED_BY(mutex_);
};
/*
GUARDED_BY(mutex_)
THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(mutex_))
__attribute__((guarded_by(mutex_))
guarded_by������Ϊ�˱�֤�̰߳�ȫ��ʹ�ø����Ժ��߳�Ҫʹ����Ӧ����������������mutex_

�̰߳�ȫע��
GUARDED_BY//�������ݳ�Ա�ܸ������ܱ����������ݵĶ�ȡ������Ҫ������ʣ���д�������Ҫ��ռ���ʡ�
*/
}  // namespace muduo
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
