// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADPOOL_H
#define MUDUO_BASE_THREADPOOL_H

#include "muduo/base/Condition.h"
#include "muduo/base/Mutex.h"
#include "muduo/base/Thread.h"
#include "muduo/base/Types.h"

#include <deque>
#include <vector>

namespace muduo
{

/**
 * ʲô���̳߳أ�
 * ��ɣ�
 * 1������߳�
 * 2��һ���̰߳�ȫ���������
 * �̳߳���ʵ������һ��������������ģ��
 * 1���߳��������ߣ���������л�ȡ����
 * 2������������������ľ���������
 *
 * ������������ģ�ͼ�飺(�����ߡ������ߡ�����)
 * 1���������������߻���
 * 2���������������߻���
 * 3���������������߻���ͬ��
 * ͨ���������ﵽ����Ŀ��
 *
 * 4����Դ����ʱ��ͨ����Ҫ�������ߵȴ�
 * 5����Դ���˺���Ҫ�а취֪ͨ�����߽�������
 * �������ͨ����������ʵ��
 *
 */

//muduo�߳���Ŀ��������ʱ���ã����̳߳�����ʱ���߳���Ŀ���Ѿ��̶�������
class ThreadPool : noncopyable
{
 public:
  typedef std::function<void ()> Task;

  explicit ThreadPool(const string& nameArg = string("ThreadPool"));
  ~ThreadPool();

  // Must be called before start().
  void setMaxQueueSize(int maxSize) { maxQueueSize_ = maxSize; }//����������������Ŀ
  void setThreadInitCallback(const Task& cb)//�����߳�ִ��ǰ�Ļص�����
  { threadInitCallback_ = cb; }

  void start(int numThreads);
  void stop();

  const string& name() const
  { return name_; }

  size_t queueSize() const;

  // Could block if maxQueueSize > 0
  // Call after stop() will return immediately.
  // There is no move-only version of std::function in C++ as of C++14.
  // So we don't need to overload a const& and an && versions
  // as we do in (Bounded)BlockingQueue.
  // https://stackoverflow.com/a/25408989
  void run(Task f);

 private:
  bool isFull() const REQUIRES(mutex_);//����
  void runInThread();//�̳߳ص��߳����к���
  Task take();//ȡ������

  mutable MutexLock mutex_;
  Condition notEmpty_ GUARDED_BY(mutex_);//����condition
  Condition notFull_ GUARDED_BY(mutex_);//δ��condition
  string name_;
  Task threadInitCallback_;//�߳�ִ��ǰ�Ļص�����
  std::vector<std::unique_ptr<muduo::Thread>> threads_;//�߳�����
  std::deque<Task> queue_ GUARDED_BY(mutex_);//�������
  size_t maxQueueSize_; //��Ϊdeque��ͨ��push_back�����߳���Ŀ�ģ�����ͨ�����max_queuesize�洢����߳���Ŀ
  bool running_; //�̳߳����б�־
};

}  // namespace muduo

#endif  // MUDUO_BASE_THREADPOOL_H
