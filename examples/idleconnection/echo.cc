#include "echo.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include <assert.h>
#include <stdio.h>

using namespace muduo;
using namespace muduo::net;


EchoServer::EchoServer(EventLoop* loop,
                       const InetAddress& listenAddr,
                       int idleSeconds)
  : server_(loop, listenAddr, "EchoServer"),
    connectionBuckets_(idleSeconds)
{
  server_.setConnectionCallback(
      std::bind(&EchoServer::onConnection, this, _1));
  server_.setMessageCallback(
      std::bind(&EchoServer::onMessage, this, _1, _2, _3));
  loop->runEvery(1.0, std::bind(&EchoServer::onTimer, this));
  connectionBuckets_.resize(idleSeconds);
  dumpConnectionBuckets();
}

void EchoServer::start()
{
  server_.start();
}

void EchoServer::onConnection(const TcpConnectionPtr& conn)
{
  LOG_INFO << "EchoServer - " << conn->peerAddress().toIpPort() << " -> "
           << conn->localAddress().toIpPort() << " is "
           << (conn->connected() ? "UP" : "DOWN");

  if (conn->connected())
  {
    EntryPtr entry(new Entry(conn));  //entry引用计数为1
    connectionBuckets_.back().insert(entry);  //entry引用计数为2
    dumpConnectionBuckets();
    WeakEntryPtr weakEntry(entry);     //entry引用计数不会变
    conn->setContext(weakEntry);       
  } 
  else
  {
    assert(!conn->getContext().empty());
    WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
    LOG_DEBUG << "Entry use_count = " << weakEntry.use_count();
  }
}   //退出之后栈上变量消失，entry引用计数减1

void EchoServer::onMessage(const TcpConnectionPtr& conn,
                           Buffer* buf,
                           Timestamp time)
{
  string msg(buf->retrieveAllAsString());
  LOG_INFO << conn->name() << " echo " << msg.size()
           << " bytes at " << time.toString();
  conn->send(msg);

  assert(!conn->getContext().empty());
  //获得entry的弱引用，这个弱引用的目的是可以获知之前entry的应用计数
  WeakEntryPtr weakEntry(boost::any_cast<WeakEntryPtr>(conn->getContext()));
  //如果entry引用计数不为0,那么加1，若为0，则weakEntry.lock()将获得一个空指针
  EntryPtr entry(weakEntry.lock());
  if (entry)
  {
    connectionBuckets_.back().insert(entry);
    dumpConnectionBuckets();
  }
}

void EchoServer::onTimer()
{
  //如果环形缓冲区满了，调用push_back的时候，会移除缓冲区第一个位置的元素，即""向前推"
  //如果是push_front的话，那么就”向后推“，移除缓冲区最后一个位置的元素
  connectionBuckets_.push_back(Bucket());
  dumpConnectionBuckets();
}

void EchoServer::dumpConnectionBuckets() const
{
  LOG_INFO << "size = " << connectionBuckets_.size();
  int idx = 0;
  for (WeakConnectionList::const_iterator bucketI = connectionBuckets_.begin();
      bucketI != connectionBuckets_.end();
      ++bucketI, ++idx)
  {
    const Bucket& bucket = *bucketI;
    printf("[%d] len = %zd : ", idx, bucket.size());
    for (const auto& it : bucket)
    {
      //如果weakConn_的引用计数为0，则到期了，为真
      bool connectionDead = it->weakConn_.expired();
      printf("%p(%ld)%s, ", get_pointer(it), it.use_count(),
          connectionDead ? " DEAD" : "");
    }
    puts("");
  }
}

