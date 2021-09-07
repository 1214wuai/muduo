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

//CAPABILITY//该宏负责指定相关的类使用线程安全检测机制，即标准文档所说的功能，用于直接修饰类
//SCOPED_CAPABILITY//负责实现RAII样式锁定的类属性，即在构造时获取能力，析构时释放能力。其他和CAPABILITY类似。
//GUARDED_BY//声明数据成员受给定功能保护。对数据的读取操作需要共享访问，而写入操作需要独占访问。
//PT_GUARDED_BY和GUARDED_BY类似，用于指针和智能指针，用户保护指针指向的数据。
//ACQUIRED_BEFORE需要在另一个能力获取之前被调用
//ACQUIRED_AFTER需要在另一个能力获取之后被调用
//REQUIRES用来修饰函数，使其调用线程必须具有对给定功能的独占访问权限。被修饰的函数在进入前必须已经持有能力，函数退出时不在持有能力。
//REQUIRES_SHARED//和REQUIRES类似，只不过REQUIRES_SHARED可以共享地获取能力
//ACQUIRE//用来修饰函数，使其调用线程必须具有对给定功能的独占访问权限。被修饰的函数在进入前必须持有能力。

//ACQUIRE_SHARED//和ACQUIRE相同，只是能力可以共享
//****//用来修饰函数，使其调用线程必须具有对给定功能的独占访问权限。被修饰的函数退出时不在持有能力。
//RELEASE_SHARED//和RELEASE相同，用于修饰释放可以共享的能力
//RELEASE_GENERIC//和RELEASE相同，用于修饰释放共享的能力和非共享的能力
//TRY_ACQUIRE//尝试获取能力T
//RY_ACQUIRE_SHARED//尝试获取共享的能力
//EXCLUDES//修饰函数一定不能具有某项能力
//ASSERT_CAPABILITY//修饰调用线程已经具有给定的能力。
//RETURN_CAPABILITY//修饰函数负责返回给定的能力
//NO_THREAD_SAFETY_ANALYSIS//修饰函数，关闭能力检查

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
 * 被mutable修饰的数据成员，可以在const成员函数中修改。
 * Mutex.h用RAII手法封装mutex的创建、销毁、加锁、解锁这四个操作
 * //================================================================
 * 类MutexLock，被CAPABILITY("mutex")修饰，且不可拷贝
 * private:
 *      pthread_mutex_t mutex_;
 *      pid_t holder_;用来存储线程被内核调用的线程ID
 * public：
 *      构造函数：把holder_置0，pthread_mutex_init(&mutex_,NULL)
 *      析构函数：把holder_置0，pthread_mutex_destroy(&mutex_)
 *      lock()-->pthread_mutex_lock(&mutex_)，上锁，同时和当前线程ID绑定
 *               assignHolder();-->将线程ID赋值给成员变量holder_
 *      unlock()-->pthread_mutex_unlock(&mutex_)
 *                 unassignHolder();将holder_置0，该线程释放锁
 *      assertLocked()-->isLockedByThisThread,判断holder_是不是等于当前线程的ID，是一个断言，
 *      如果相等，程序继续运行，else：报错&&终止程序
 *
 *      //===========================================================
 *      内部类UnassignGuard，不可拷贝
 *      private：
 *          MutexLock& owner_;
 *      public：
 *          构造函数-->将owner_的holder置0，也就是将锁和线程ID的绑定解除
 *          析构函数-->将线程ID赋值给成员变量holder_
 *
 * //===============================================================
 * 类MutexLockGuard，被SCOPED_CAPABILITY修饰，且不可拷贝
 * private：
 *      MutexLock& mutex_;
 * public：
 *      构造函数-->mutex_.lock();
 *      析构函数-->mutex_.unassignHolderlock();
 *
 *
 * SCOPED_CAPABILITY是实现RAII样式锁定的类的属性，其中在构造函数中获取能力，并在析构函数中释放。
 * 此类类需要特殊处理，因为构造函数和析构函数通过不同的名称来引用功能
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

  //UnassignGuard在condition中使用
  /*
  在pthread_cond_wait()前构造该类，将锁的持有者清零，在pthread_cond_wait()返回后调用该类的析构函数，重新占用锁时将锁的持有者该为当前线程。
  */
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
//防止出现MutexlockGuard(mutex);形式的调用，遗漏变量名，产生一个临时变量又马上销毁了，导致锁不住临界区
//正确的调用：MutexlockGuard lock(mutex)
//临界区


#endif  // MUDUO_BASE_MUTEX_H
