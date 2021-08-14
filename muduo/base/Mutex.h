// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_MUTEX_H
#define MUDUO_BASE_MUTEX_H

#include "muduo/base/CurrentThread.h"
#include "muduo/base/noncopyable.h"
#include <assert.h>
#include <pthread.h>

// Thread safety annotations {
// https://clang.llvm.org/docs/ThreadSafetyAnalysis.html

// Enable thread safety attributes only with clang.
// The attributes can be safely erased when compiling with other compilers.

//CAPABILITY//�ú긺��ָ����ص���ʹ���̰߳�ȫ�����ƣ�����׼�ĵ���˵�Ĺ��ܣ�����ֱ��������
//SCOPED_CAPABILITY//����ʵ��RAII��ʽ�����������ԣ����ڹ���ʱ��ȡ����������ʱ�ͷ�������������CAPABILITY���ơ�
//GUARDED_BY//�������ݳ�Ա�ܸ������ܱ����������ݵĶ�ȡ������Ҫ������ʣ���д�������Ҫ��ռ���ʡ�
//PT_GUARDED_BY��GUARDED_BY���ƣ�����ָ�������ָ�룬�û�����ָ��ָ������ݡ�
//ACQUIRED_BEFORE��Ҫ����һ��������ȡ֮ǰ������
//ACQUIRED_AFTER��Ҫ����һ��������ȡ֮�󱻵���
//REQUIRES�������κ�����ʹ������̱߳�����жԸ������ܵĶ�ռ����Ȩ�ޡ������εĺ����ڽ���ǰ�����Ѿ����������������˳�ʱ���ڳ���������
//REQUIRES_SHARED//��REQUIRES���ƣ�ֻ����REQUIRES_SHARED���Թ���ػ�ȡ����
//ACQUIRE//�������κ�����ʹ������̱߳�����жԸ������ܵĶ�ռ����Ȩ�ޡ������εĺ����ڽ���ǰ�������������

//ACQUIRE_SHARED//��ACQUIRE��ͬ��ֻ���������Թ���
//****//�������κ�����ʹ������̱߳�����жԸ������ܵĶ�ռ����Ȩ�ޡ������εĺ����˳�ʱ���ڳ���������
//RELEASE_SHARED//��RELEASE��ͬ�����������ͷſ��Թ��������
//RELEASE_GENERIC//��RELEASE��ͬ�����������ͷŹ���������ͷǹ��������
//TRY_ACQUIRE//���Ի�ȡ����T
//RY_ACQUIRE_SHARED//���Ի�ȡ���������
//EXCLUDES//���κ���һ�����ܾ���ĳ������
//ASSERT_CAPABILITY//���ε����߳��Ѿ����и�����������
//RETURN_CAPABILITY//���κ������𷵻ظ���������
//NO_THREAD_SAFETY_ANALYSIS//���κ������ر��������

#if defined(__clang__) && (!defined(SWIG))
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)   // no-op
#endif

#define CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(capability(x))

#define SCOPED_CAPABILITY \
  THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)

#define GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define PT_GUARDED_BY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))

#define ACQUIRED_BEFORE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define ACQUIRED_AFTER(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define REQUIRES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define REQUIRES_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))

#define ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(acquire_shared_capability(__VA_ARGS__))

#define RELEASE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#define RELEASE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(release_shared_capability(__VA_ARGS__))

#define TRY_ACQUIRE(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_capability(__VA_ARGS__))

#define TRY_ACQUIRE_SHARED(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(try_acquire_shared_capability(__VA_ARGS__))

#define EXCLUDES(...) \
  THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))

#define ASSERT_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_capability(x))

#define ASSERT_SHARED_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_capability(x))

#define RETURN_CAPABILITY(x) \
  THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))

#define NO_THREAD_SAFETY_ANALYSIS \
  THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)

// End of thread safety annotations }

#ifdef CHECK_PTHREAD_RETURN_VALUE

#ifdef NDEBUG
__BEGIN_DECLS
extern void __assert_perror_fail (int errnum,
                                  const char *file,
                                  unsigned int line,
                                  const char *function)
    noexcept __attribute__ ((__noreturn__));
__END_DECLS
#endif

#define MCHECK(ret) ({ __typeof__ (ret) errnum = (ret);         \
                       if (__builtin_expect(errnum != 0, 0))    \
                         __assert_perror_fail (errnum, __FILE__, __LINE__, __func__);})

#else  // CHECK_PTHREAD_RETURN_VALUE

#define MCHECK(ret) ({ __typeof__ (ret) errnum = (ret);         \
                       assert(errnum == 0); (void) errnum;})

#endif // CHECK_PTHREAD_RETURN_VALUE

/*
 * CountDownLatch.h
 * ��mutable���ε����ݳ�Ա��������const��Ա�������޸ġ�
 * Mutex.h��RAII�ַ���װmutex�Ĵ��������١��������������ĸ�����
 * //================================================================
 * ��MutexLock����CAPABILITY("mutex")���Σ��Ҳ��ɿ���
 * private:
 *      pthread_mutex_t mutex_;
 *      pid_t holder_;�����洢�̱߳��ں˵��õ��߳�ID
 * public��
 *      ���캯������holder_��0��pthread_mutex_init(&mutex_,NULL)
 *      ������������holder_��0��pthread_mutex_destroy(&mutex_)
 *      lock()-->pthread_mutex_lock(&mutex_)��������ͬʱ�͵�ǰ�߳�ID��
 *               assignHolder();-->���߳�ID��ֵ����Ա����holder_
 *      unlock()-->pthread_mutex_unlock(&mutex_)
 *                 unassignHolder();��holder_��0�����߳��ͷ���
 *      assertLocked()-->isLockedByThisThread,�ж�holder_�ǲ��ǵ��ڵ�ǰ�̵߳�ID����һ�����ԣ�
 *      �����ȣ�����������У�else������&&��ֹ����
 *
 *      //===========================================================
 *      �ڲ���UnassignGuard�����ɿ���
 *      private��
 *          MutexLock& owner_;
 *      public��
 *          ���캯��-->��owner_��holder��0��Ҳ���ǽ������߳�ID�İ󶨽��
 *          ��������-->���߳�ID��ֵ����Ա����holder_
 *
 * //===============================================================
 * ��MutexLockGuard����SCOPED_CAPABILITY���Σ��Ҳ��ɿ���
 * private��
 *      MutexLock& mutex_;
 * public��
 *      ���캯��-->mutex_.lock();
 *      ��������-->mutex_.unassignHolderlock();
 *
 *
 * SCOPED_CAPABILITY��ʵ��RAII��ʽ������������ԣ������ڹ��캯���л�ȡ���������������������ͷš�
 * ��������Ҫ���⴦����Ϊ���캯������������ͨ����ͬ�����������ù���
 *
 * //CountDownLatch.cchttps://blog.csdn.net/weixin_40471400/article/details/105589218
 */

namespace muduo
{

// Use as data member of a class, eg.
//
// class Foo
// {
//  public:
//   int size() const;
//
//  private:
//   mutable MutexLock mutex_;
//   std::vector<int> data_ GUARDED_BY(mutex_);
// };
class CAPABILITY("mutex") MutexLock : noncopyable
{
 public:
  MutexLock()
    : holder_(0)
  {
    MCHECK(pthread_mutex_init(&mutex_, NULL));
  }

  ~MutexLock()
  {
    assert(holder_ == 0);
    MCHECK(pthread_mutex_destroy(&mutex_));
  }

  // must be called when locked, i.e. for assertion
  bool isLockedByThisThread() const
  {
    return holder_ == CurrentThread::tid();
  }

  void assertLocked() const ASSERT_CAPABILITY(this)
  {
    assert(isLockedByThisThread());
  }

  // internal usage

  void lock() ACQUIRE()
  {
    MCHECK(pthread_mutex_lock(&mutex_));
    assignHolder();
  }

  void unlock() RELEASE()
  {
    unassignHolder();
    MCHECK(pthread_mutex_unlock(&mutex_));
  }

  pthread_mutex_t* getPthreadMutex() /* non-const */
  {
    return &mutex_;
  }

 private:
  friend class Condition;

  class UnassignGuard : noncopyable
  {
   public:
    explicit UnassignGuard(MutexLock& owner)
      : owner_(owner)
    {
      owner_.unassignHolder();
    }

    ~UnassignGuard()
    {
      owner_.assignHolder();
    }

   private:
    MutexLock& owner_;
  };

  void unassignHolder()
  {
    holder_ = 0;
  }

  void assignHolder()
  {
    holder_ = CurrentThread::tid();
  }

  pthread_mutex_t mutex_;
  pid_t holder_;
};

// Use as a stack variable, eg.
// int Foo::size() const
// {
//   MutexLockGuard lock(mutex_);
//   return data_.size();
// }
class SCOPED_CAPABILITY MutexLockGuard : noncopyable
{
 public:
  explicit MutexLockGuard(MutexLock& mutex) ACQUIRE(mutex)
    : mutex_(mutex)
  {
    mutex_.lock();
  }

  ~MutexLockGuard() RELEASE()
  {
    mutex_.unlock();
  }

 private:

  MutexLock& mutex_;
};

}  // namespace muduo

// Prevent misuse like:
// MutexLockGuard(mutex_);
// A tempory object doesn't hold the lock for long!
#define MutexLockGuard(x) error "Missing guard object name"

#endif  // MUDUO_BASE_MUTEX_H
