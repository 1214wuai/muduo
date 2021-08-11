// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREAD_H
#define MUDUO_BASE_THREAD_H

#include "muduo/base/Atomic.h"
#include "muduo/base/CountDownLatch.h"
#include "muduo/base/Types.h"

#include <functional>
#include <memory>
#include <pthread.h>

namespace muduo
{
/*
��static��Ա�����ڵ���ν�ʺ��������õ�ʱ���޷�����thisָ��
ν���Ƿ���boolֵ�ĺ���
�º����������������ع����operator()�����

��1��ʲô��ֵ���壿
��νֵ������ָĿ�������Դ���󿽱����ɣ������ɺ���Դ������ȫ�޹أ��˴˶������ڣ��ı以��Ӱ�졣
����int���ͱ����໥����һ����C++���������ͣ�bool/int/double/char������ֵ���壬
��׼�����complex<>��pair<>��vector<>��map<>��string�ȵ�����Ҳ����ֵ���塣
����֮�����Դ������ȫ�����ϵ��

��2��ʲô�Ƕ������壿
��������Ҳ��ָ�����壬��������ȡ�
ͨ����ָһ��Ŀ�������Դ���󿽱����ɣ������ɺ���Դ����֮����Ȼ����ײ���Դ��
���κ�һ���ĸı䶼����֮�ı���һ����
���������ָ���Ա�������Զ�������Ĭ�Ͽ������캯���¶������֮����еĿ�����
������Ŀ������Դ�����ָ���Ա������ָ��ͬһ���ڴ����ݡ�
���������һ��������������һ�������ָ���Ա�ͻ���Ϊ������ʵ������ָ�룡
�ֱ��磬Thread�Ƕ������壬����Thread��������ģ�Ҳ�Ǳ���ֹ�ģ�
��ΪThread�����̣߳�����һ��Thread���󲢲�����ϵͳ����һ��һģһ�����̡߳�
 */
class Thread : noncopyable
{
 public:
  typedef std::function<void ()> ThreadFunc;

  // explicit����ֻ��һ���������๹�캯��,
  // �����ж�����������ǳ���һ�������������Ĳ�������Ĭ��ֵ�Ĺ��캯����
  // ���������Ǳ����ù��캯������ʾ��, ������ʽ��,
  // ���������Ǳ������캯������ʽ��ʽ��ʾ�ġ����๹�캯��Ĭ��Ϊ��ʽ��
  // explicit��ֹ���๹�캯������ʽ�Զ�ת��
  // �������Ӧ����һ���ؼ�����implicit, ��˼�����ص�,�๹�캯��Ĭ������¼�����Ϊimplicit(��ʽ).
  explicit Thread(ThreadFunc, const string& name = string());
  // FIXME: make it movable in C++11
  ~Thread();

  void start();
  int join(); // return pthread_join()

  bool started() const { return started_; }
  // pthread_t pthreadId() const { return pthreadId_; }
  pid_t tid() const { return tid_; }
  const string& name() const { return name_; }

  static int numCreated() { return numCreated_.get(); }

 private:
  void setDefaultName();

  bool       started_;
  bool       joined_;
  pthread_t  pthreadId_;
  pid_t      tid_;
  ThreadFunc func_;
  string     name_;
  CountDownLatch latch_;

  static AtomicInt32 numCreated_;
};

}  // namespace muduo
#endif  // MUDUO_BASE_THREAD_H
