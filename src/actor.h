#ifndef ACTOR_H_
#define ACTOR_H_

#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <iostream>
#include <queue>
#include <string>

#include "future.h"

class Supervisor;

using Message = std::shared_ptr<void>;

class Scheduler;
using SchedulerPtr = boost::intrusive_ptr<Scheduler>;

class ProcessContext;
using ProcessContextPtr = boost::intrusive_ptr<ProcessContext>;

class Pid {
 public:
  Pid(SchedulerPtr sched, uint64_t pid) : scheduler_(sched), pid_(pid) {}
  void Send(Message message);
  strand_ptr_t Strand() const;

  friend bool operator<(Pid a, Pid b) { return a.pid_ < b.pid_; }
  friend bool operator>(Pid a, Pid b) { return a.pid_ > b.pid_; }
  friend bool operator<=(Pid a, Pid b) { return a.pid_ <= b.pid_; }
  friend bool operator>=(Pid a, Pid b) { return a.pid_ >= b.pid_; }
  friend bool operator==(Pid a, Pid b) { return a.pid_ == b.pid_; }
  friend bool operator!=(Pid a, Pid b) { return a.pid_ != b.pid_; }

 private:
  SchedulerPtr scheduler_;
  uint64_t pid_ = 0;
};

class ProcessContext : public boost::intrusive_ref_counter<ProcessContext> {
 public:
  ProcessContext(SchedulerPtr sched, Pid pid) : scheduler_(sched), pid_(pid) {}
  FuturePtr<Message> Receive();
  strand_ptr_t Strand() const;
  void Lock(FuturePtr<void> future);

 private:
  SchedulerPtr scheduler_;
  Pid pid_;
};

class Scheduler : public boost::intrusive_ref_counter<Scheduler> {
 public:
  Scheduler(boost::asio::io_context& ioc)
      : strand_(new boost::asio::io_context::strand(ioc)) {}
  strand_ptr_t Strand() const { return strand_; }
  Pid Start(std::function<FuturePtr<void>(ProcessContextPtr)> process) {
    auto sched = SchedulerPtr(this);
    auto pid = Pid(sched, ++pid_counter_);
    ProcessContextPtr pc(new ProcessContext(sched, pid));
    data_[pid].pc = pc;
    data_[pid].process = std::move(process);
    boost::asio::defer(*Strand(), [self = sched, pid]() {
      (self->data_[pid].process)(self->data_[pid].pc)
          ->Then<void>([self, pid]() {
            // 実行完了
            self->data_.erase(pid);
          });
    });
    return pid;
  }

  FuturePtr<Message> Receive(Pid pid) {
    if (!data_[pid].queue.empty()) {
      Message m = data_[pid].queue.front();
      data_[pid].queue.pop();
      return Future::Resolve(Strand(), m);
    }
    return Future::Create<Message>(
        Strand(), [self = SchedulerPtr(this), pid](auto resolve, auto reject) {
          self->data_[pid].receiver = resolve;
        });
  }

  // 選択的受信
  // FuturePtr<Message> Receive(Pid pid, std::function<bool (Message)> cond);

  void Send(Pid pid, Message message) {
    if (data_[pid].receiver) {
      data_[pid].receiver(message);
      data_[pid].receiver = nullptr;
      return;
    }
    data_[pid].queue.push(message);
  }

  void Lock(Pid pid, FuturePtr<void> future) {
    auto& cnt = data_[pid].locked_counter;
    auto& locked = data_[pid].locked;
    locked[cnt] = future->Then<void>([self = SchedulerPtr(this), pid, cnt]() {
      self->data_[pid].locked.erase(cnt);
    });
    cnt += 1;
  }

  //FuturePtr<void> Exit(Pid pid) {
  //  // ロックされてる要素が無ければそのまま cancel
  //  // ロッグされてる要素の終了を待ってからメイン処理を cancel
  //  // タイムアウトも付けられるようにするj
  //}

 private:
  struct ProcessData {
    ProcessContextPtr pc;
    std::function<void(Message)> receiver;
    std::queue<Message> queue;
    std::function<FuturePtr<void>(ProcessContextPtr)> process;
    uint64_t locked_counter = 0;
    std::map<uint64_t, FuturePtr<void>> locked;
  };
  std::map<Pid, ProcessData> data_;
  strand_ptr_t strand_;
  uint64_t pid_counter_ = 0;
};

void Pid::Send(Message message) {
  scheduler_->Send(*this, message);
}
strand_ptr_t Pid::Strand() const {
  return scheduler_->Strand();
}

strand_ptr_t ProcessContext::Strand() const {
  return scheduler_->Strand();
}
FuturePtr<Message> ProcessContext::Receive() {
  return scheduler_->Receive(pid_);
}
void ProcessContext::Lock(FuturePtr<void> future) {
  return scheduler_->Lock(pid_, future);
}

void test_process() {
  boost::asio::io_context ioc;
  SchedulerPtr sched(new Scheduler(ioc));
  // 以下の Erlang コードと大体同じ
  // Pid = spawn(fun() ->
  //               receive
  //                 M -> io:format("~p~n", M)
  //               end
  //             end),
  // Pid ! <<"hello">>.
  Pid pid = sched->Start([](ProcessContextPtr pc) {
    return Future::Create<void>(
        pc->Strand(), [pc](std::function<void()> resolve,
                           std::function<void(std::exception_ptr)> reject) {
          pc->Receive()->Then<void>([resolve](Message m) {
            auto message = *(std::string*)m.get();
            std::cout << message << std::endl;
            resolve();
          });
        });
  });
  pid.Send(Message(new std::string("hello")));

  ioc.run();
}

class Actor {
 public:
  template <class M>
  void Send(M message) {
    sup_->SendActor(std::move(message));
  }

 private:
  uint64_t id_ = 0;
  Supervisor* sup_;
};

template <class M>
class ActorBase {
 public:
  virtual FuturePtr<void> OnInit() = 0;
  virtual FuturePtr<void> OnMessage(M message) = 0;
  virtual FuturePtr<void> OnTerminate() = 0;
};

class Supervisor {
 public:
  Supervisor(boost::asio::io_context& ioc) : ioc_(ioc) {}
  template <class A, class... Args>
  Actor CreateActor(Args&&... args) {
    return Actor();
  }

  template <class M>
  void SendActor(M message) {}

 private:
  boost::asio::io_context& ioc_;
};

class MyActor : public ActorBase<int> {
 public:
  FuturePtr<void> OnInit() override {}
  FuturePtr<void> OnMessage(int message) override {}
  FuturePtr<void> OnTerminate() override {}
};

void test_actor() {
  boost::asio::io_context ioc;
  Supervisor sup(ioc);
  Actor actor = sup.CreateActor<MyActor>();
}

#endif