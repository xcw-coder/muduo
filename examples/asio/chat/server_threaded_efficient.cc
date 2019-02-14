#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <set>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

//核心思想是用shared_ptr来实现copy on write,从而减小锁竞争，降低了第二条消息达到各个客户端的延迟
//但highperformance可以在此基础上缩小一条消息到达第一个连接至最后一个连接 的时间差
class ChatServer : noncopyable
{
 public:
  ChatServer(EventLoop* loop,
             const InetAddress& listenAddr)
  : server_(loop, listenAddr, "ChatServer"),
    codec_(std::bind(&ChatServer::onStringMessage, this, _1, _2, _3)),
    connections_(new ConnectionList)   //ConnectionList的引用计数初始化为1
  {
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&LengthHeaderCodec::onMessage, &codec_, _1, _2, _3));
  }

  void setThreadNum(int numThreads)
  {
    server_.setThreadNum(numThreads);
  }

  void start()
  {
    server_.start();
  }

 private:
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
        << conn->peerAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");

    MutexLockGuard lock(mutex_);
    if (!connections_.unique())      //说明引用计数大于1，说明要写的时候，已经有其他线程正在读
    {
      //new ConnectionList(*connections_)这段代码意味着拷贝了一份ConnectionList，并且这个新的ConnectionList用这个connection_来接管
      //假设此之前connections_引用计数为2，那么经过下面这行代码之后connections_引用计数为1，而原来的那个ConnectionList的引用计数也减为1，
      //而当读操作完成之后，connections这个栈变量将销毁，i那么引用计数变为0，原先的那个ConnectionList就会被销毁掉，所以不会存在2份副本。
      connections_.reset(new ConnectionList(*connections_));
    }
    assert(connections_.unique());

    if (conn->connected())
    {
      connections_->insert(conn);
    }
    else
    {
      connections_->erase(conn);
    }
  }

  typedef std::set<TcpConnectionPtr> ConnectionList;
  typedef std::shared_ptr<ConnectionList> ConnectionListPtr;

  void onStringMessage(const TcpConnectionPtr&,
                       const string& message,
                       Timestamp)
  { //定义一个栈上的变量，引用计数加1，mutex保护的临界区大大缩短
    ConnectionListPtr connections = getConnectionList();;
    //可能这里会有疑问，不受mutex保护，写者更改了连接列表怎么办呢？
    //实际上，写者是在另外一个副本上修改，所以无需担心。
    for (ConnectionList::iterator it = connections->begin();
        it != connections->end();
        ++it)
    {
      codec_.send(get_pointer(*it), message);
    }
    //assert(!connections.unique())  这个断言在这里不一定成立
  }    //当connections这个栈上的变量销毁的时候，引用计数减1

  //可能多个线程都在onStringMessage，所以可能多个线程都会访问connections_
  ConnectionListPtr getConnectionList()
  {
    MutexLockGuard lock(mutex_);
    return connections_;
  }

  TcpServer server_;
  LengthHeaderCodec codec_;
  MutexLock mutex_;
  ConnectionListPtr connections_ GUARDED_BY(mutex_);
};

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    ChatServer server(&loop, serverAddr);
    if (argc > 2)
    {
      server.setThreadNum(atoi(argv[2]));
    }
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port [thread_num]\n", argv[0]);
  }
}

