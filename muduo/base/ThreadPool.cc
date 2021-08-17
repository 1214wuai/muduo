// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include "muduo/base/ThreadPool.h"

#include "muduo/base/Exception.h"

#include <assert.h>
#include <stdio.h>

using namespace muduo;

ThreadPool::ThreadPool(const string& nameArg)
  : mutex_(),
    notEmpty_(mutex_),
    notFull_(mutex_),
    name_(nameArg),
    maxQueueSize_(0),
    running_(false)
{
}

ThreadPool::~ThreadPool()
{
  if (running_)  //����̳߳������У��Ǿ�Ҫ�����ڴ洦����stop()������ִ��
  {
    stop();
  }
}

void ThreadPool::start(int numThreads)
{
  assert(threads_.empty());  //ȷ��δ������
  running_ = true;  //������־
  threads_.reserve(numThreads);  //Ԥ��numThreads���ռ�
  /*
   * vector���ݷ�ʽ��
   * Ԫ�����ʱ���ݣ�VS1.5����Linux2��
   * reserve(int  new_size)   //ֻ�ı�capacity��new_size����size���䣬�ռ��ڲ���������Ԫ�ض���
   * resize(int  new_size /*, int  init_value*/)   //�ı�capacity(��new_size��)��size��new_size������������
  */
  for (int i = 0; i < numThreads; ++i)  // ��������numThreads���߳�
  {
    char id[32];  //�洢�߳�No.,��1��ʼ
    snprintf(id, sizeof id, "%d", i+1);
    threads_.emplace_back(new muduo::Thread(  //boost::bind�ڰ����ڲ���Աʱ���ڶ����������������ʵ��
          std::bind(&ThreadPool::runInThread, this), name_+id));//runInThread��ÿ���̵߳��߳����к������߳�Ϊִ����������»�����
    threads_[i]->start();//����ÿ���̣߳����������̳߳ص�������������ʱ������л�Ϊ�գ���˻�����
  }
  if (numThreads == 0 && threadInitCallback_)
  {
    threadInitCallback_();
  }
}

void ThreadPool::stop()
{
  //���̳߳�������־����Ϊfalse
  //���������߳�
  //�ȴ��߳�
  {
  MutexLockGuard lock(mutex_);
  running_ = false;
  notEmpty_.notifyAll();
  notFull_.notifyAll();
  }
  for (auto& thr : threads_)
  {
    thr->join();
  }
}

size_t ThreadPool::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return queue_.size();
}

// �������������������񣬿��Ա��κ��̵߳���
void ThreadPool::run(Task task)
{
  if (threads_.empty())//����߳�����Ϊ�գ������߳��Լ�ִ�и�����
  {
    task();
  }
  else
  {
    MutexLockGuard lock(mutex_);
    while (isFull() && running_)//�������������ˣ�����Ҫ�ȴ�������в�����ʱ��
    {
      notFull_.wait();
    }
    if (!running_) return;
    assert(!isFull());

    queue_.push_back(std::move(task));
    notEmpty_.notify();  // ����һ��������
  }
}

// �����������ȡ��һ������һ��ֻ���̳߳����̵߳�ִ�к�������
ThreadPool::Task ThreadPool::take()
{
  MutexLockGuard lock(mutex_);  // ����
  // always use a while-loop, due to spurious wakeup
  while (queue_.empty() && running_)
  {
    notEmpty_.wait();  // �ж��Ƿ������������Ϊ�գ���������������ͷ���������
  }
  Task task;
  if (!queue_.empty())
  {
    task = queue_.front();  // �����������ȡ������
    queue_.pop_front();
    if (maxQueueSize_ > 0)
    {
      notFull_.notify();
    }
  }
  return task;  // ��������
}

bool ThreadPool::isFull() const
{
  mutex_.assertLocked();//�ж����ǲ��Ǳ���ǰ�̻߳�ȡ
  return maxQueueSize_ > 0 && queue_.size() >= maxQueueSize_;
}

//�̳߳��ڵ��߳�ִ�еĺ���
void ThreadPool::runInThread()
{
  try
  {
    if (threadInitCallback_)
    {
      threadInitCallback_();
    }
    while (running_)
    {
      // �����������ȡ��һ������
      Task task(take());
      if (task)
      {
        // ִ������
        task();
      }
    }
  }
  catch (const Exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
    abort();
  }
  catch (const std::exception& ex)
  {
    fprintf(stderr, "exception caught in ThreadPool %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    abort();
  }
  catch (...)
  {
    fprintf(stderr, "unknown exception caught in ThreadPool %s\n", name_.c_str());
    throw; // rethrow
  }
}

