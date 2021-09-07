// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
/*
 * CountDownLatch����ʹһ�������̵߳ȴ������̸߳���ִ����Ϻ���ִ�С�
 * CountDownLatch ������һ������������һ���������У� ����������ֵ�ݼ�Ϊ0֮ǰ����������������̴߳��ڹ���״̬��
 * ���������ݼ���0ʱ�ỽ���������������̣߳�����ļ�������һ����־�����Ա�ʾһ������һ���̣߳�Ҳ���Ա�ʾһ������ʱ����
 * CountDownLatch���Խ����Щһ�����߶���߳���ִ��֮ǰ����������ĳЩ��Ҫ��ǰ��ҵ����ִ�еĳ�����
 */
namespace muduo
{

class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count);//���췽��������һ��ֵΪcount �ļ�������

  void wait();//������ǰ�̣߳�����ǰ�̼߳����������С�

  void countDown();//�Լ��������еݼ�1���������������ݼ���0ʱ����ǰ�̻߳�ȥ��������������������̡߳�

  int getCount() const;

 private:
  mutable MutexLock mutex_;//��mutable���εĳ�Ա����������const��Ա�����б��޸�
  Condition condition_ GUARDED_BY(mutex_);//��������
  int count_ GUARDED_BY(mutex_);//������
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
