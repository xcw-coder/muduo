#include <muduo/base/ThreadPool.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Logging.h>

#include <stdio.h>
#include <unistd.h>  // usleep

void print()
{
  printf("tid=%d\n", muduo::CurrentThread::tid());
}

void printString(const std::string& str)
{
  LOG_INFO << str;
  usleep(100*1000);
}

void test(int maxSize)
{
  LOG_WARN << "Test ThreadPool with max queue size = " << maxSize;
  muduo::ThreadPool pool("MainThreadPool");
  pool.setMaxQueueSize(maxSize);
  pool.start(5);

  LOG_WARN << "Adding";
  //往任务队列里添加任务
  pool.run(print);
  pool.run(print);
  for (int i = 0; i < 100; ++i)
  {
    char buf[32];
    snprintf(buf, sizeof buf, "task %d", i);
    pool.run(std::bind(printString, std::string(buf)));
  }
  LOG_WARN << "Done";

  muduo::CountDownLatch latch(1);
  //没有实战意义，但是展示了我们可以用std::bind来绑定类的成员函数从而实现
  //作为线程的执行函数
  pool.run(std::bind(&muduo::CountDownLatch::countDown, &latch));
  latch.wait();
  pool.stop();
}

int main()
{
  test(0);
  test(1);
  test(5);
  test(10);
  test(50);
}
