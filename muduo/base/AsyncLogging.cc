// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/AsyncLogging.h>
#include <muduo/base/LogFile.h>
#include <muduo/base/Timestamp.h>

#include <stdio.h>

using namespace muduo;

AsyncLogging::AsyncLogging(const string& basename,
                           off_t rollSize,
                           int flushInterval)
  : flushInterval_(flushInterval),
    running_(false),
    basename_(basename),
    rollSize_(rollSize),
    thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
    latch_(1),
    mutex_(),
    cond_(mutex_),
    currentBuffer_(new Buffer),
    nextBuffer_(new Buffer),
    buffers_()
{
  currentBuffer_->bzero();
  nextBuffer_->bzero();
  buffers_.reserve(16);
}

//这个函数是把日志信息append到前端的内存里面   前端
void AsyncLogging::append(const char* logline, int len)
{
  muduo::MutexLockGuard lock(mutex_);
  if (currentBuffer_->avail() > len)
  {
    //当前的缓冲区可写字节数是否大于len
    currentBuffer_->append(logline, len);
  }
  else
  {
    //当前缓冲区不够写，将当前缓冲区添加到待写入文件的已填满的缓冲区
    //这里使用移动语义之后，currentBuffer_不再有指向了
    buffers_.push_back(std::move(currentBuffer_));

    //将当前缓冲区设置为预备缓冲区
    if (nextBuffer_)
    {
      //使用移动语义，currentBuffer_指向了nextBuffer_所指向的缓冲区，nextBuffer_不在有指向了
      currentBuffer_ = std::move(nextBuffer_);
    }
    else
    {
      //这种情况，极少发生，前端写入速度太快，一下子把两块缓冲区都写完了，
      //那么，只好分配一块新的缓冲区
      currentBuffer_.reset(new Buffer); // Rarely happens
    }
    currentBuffer_->append(logline, len);
    cond_.notify();    //有一块比较大的缓冲区写满了，这个兄弟就通知后端开始写入日志，
                       //而不是一有消息就通知写，这样效率太底
  }
}

//后端
void AsyncLogging::threadFunc()
{
  assert(running_ == true);
  latch_.countDown();
  LogFile output(basename_, rollSize_, false);
  //准备了两块空闲缓冲区
  BufferPtr newBuffer1(new Buffer);
  BufferPtr newBuffer2(new Buffer);
  newBuffer1->bzero();
  newBuffer2->bzero();
  BufferVector buffersToWrite;
  buffersToWrite.reserve(16);
  while (running_)
  {
    assert(newBuffer1 && newBuffer1->length() == 0);
    assert(newBuffer2 && newBuffer2->length() == 0);
    assert(buffersToWrite.empty());

    //交换buffers_和buffersToWrite之后，离开缓冲区
    {
      muduo::MutexLockGuard lock(mutex_);
      //即使是虚假的唤醒，我们也写日志文件，这个是可以的，所以用了非常规做法
      if (buffers_.empty())  // unusual usage!  非常规的做法，不能解决虚假唤醒
      {
        //等待前端写满了一个或者多个buffer，或者一个超时时间到来
        cond_.waitForSeconds(flushInterval_);
      }
      //将当前缓冲区移入buffers_，这个时候currentBuffer_已经没有指向了
      buffers_.push_back(std::move(currentBuffer_));
      //将空闲的newBuffer1置为当前缓冲区
      currentBuffer_ = std::move(newBuffer1);
      //buffer_与buffersToWrite交换，这样后面的代码在写日志的时候
      //可以在临界区之外安全的访问buffersToWrite
      buffersToWrite.swap(buffers_);
      if (!nextBuffer_)
      {
        //确保前端始终有一个预备buffer可供调配，减少前端临界区分配内存的概率，缩短前端临界区长度
        nextBuffer_ = std::move(newBuffer2);
      }
    }

    assert(!buffersToWrite.empty());

    //消息堆积
    //前端陷入死循环，拼命发送日志消息，超过后端的处理能力，这就是典型的生产速度
    //超过消费速度的问题，会造成数据在内存中堆积，严重时引发性能问题（可用内存不足）
    //或程序崩溃(分配内存失败)
    if (buffersToWrite.size() > 25)
    {
      char buf[256];
      snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
               Timestamp::now().toFormattedString().c_str(),
               buffersToWrite.size()-2);
      fputs(buf, stderr);
      output.append(buf, static_cast<int>(strlen(buf)));
      //丢掉多余日志，以腾出内存，仅保留两块缓冲区
      buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end());
    }

    for (const auto& buffer : buffersToWrite)
    {
      // FIXME: use unbuffered stdio FILE ? or use ::writev ?
      output.append(buffer->data(), buffer->length());
    }

    if (buffersToWrite.size() > 2)
    {
      // drop non-bzero-ed buffers, avoid trashing
      //仅保存两个buffer,用于newBuffer1与newBuffer2
      buffersToWrite.resize(2);
    }

    if (!newBuffer1)
    {
      assert(!buffersToWrite.empty());
      newBuffer1 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer1->reset();
    }

    if (!newBuffer2)
    {
      assert(!buffersToWrite.empty());
      newBuffer2 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer2->reset();
    }

    buffersToWrite.clear();
    output.flush();
  }
  output.flush();
}

