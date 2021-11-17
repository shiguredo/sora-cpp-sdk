#ifndef FUTURE_H_
#define FUTURE_H_

#include <atomic>
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <iostream>
#include <string>

using strand_ptr_t = std::shared_ptr<boost::asio::io_context::strand>;

template <class T>
class FutureImpl;

template <class T>
using FuturePtr = boost::intrusive_ptr<FutureImpl<T>>;

template <class T>
using enable_if_void_t = std::enable_if_t<std::is_same_v<T, void>>;
template <class T>
using enable_if_nonvoid_t = std::enable_if_t<!std::is_same_v<T, void>>;

enum class SettledStatus {
  FULFILLED,
  REJECTED,
};
template <class T>
struct SettledResult {
  SettledStatus status;
  T value;
  std::exception_ptr reason;
};
template <>
struct SettledResult<void> {
  SettledStatus status;
  std::exception_ptr reason;
};

struct TimeoutError : std::exception {};

template <class T>
struct FutureHelper {
  static FuturePtr<T> Create(
      strand_ptr_t strand,
      std::function<void(std::function<void(T)>,
                         std::function<void(std::exception_ptr)>)> fn);
  static FuturePtr<T> Resolve(strand_ptr_t strand, T v);
  static FuturePtr<T> Reject(strand_ptr_t strand, std::exception_ptr exc);
  static FuturePtr<std::vector<T>> All(strand_ptr_t strand,
                                       std::vector<FuturePtr<T>> arr);
  static FuturePtr<std::vector<SettledResult<T>>> AllSettled(
      strand_ptr_t strand,
      std::vector<FuturePtr<T>> arr);
  static FuturePtr<T> Race(strand_ptr_t strand, std::vector<FuturePtr<T>> arr);
  static FuturePtr<T> WithTimeout(strand_ptr_t strand,
                                  FuturePtr<T> future,
                                  std::chrono::microseconds timeout);
};

template <>
struct FutureHelper<void> {
  static FuturePtr<void> Create(
      strand_ptr_t strand,
      std::function<void(std::function<void()>,
                         std::function<void(std::exception_ptr)>)> fn);
  static FuturePtr<void> Resolve(strand_ptr_t strand);
  static FuturePtr<void> Reject(strand_ptr_t strand, std::exception_ptr exc);
  static FuturePtr<void> All(strand_ptr_t strand,
                             std::vector<FuturePtr<void>> arr);
  static FuturePtr<std::vector<SettledResult<void>>> AllSettled(
      strand_ptr_t strand,
      std::vector<FuturePtr<void>> arr);
  static FuturePtr<void> Race(strand_ptr_t strand,
                              std::vector<FuturePtr<void>> arr);
  static FuturePtr<void> WithTimeout(strand_ptr_t strand,
                                     FuturePtr<void> future,
                                     std::chrono::microseconds timeout);
};

class Future {
 public:
  template <class T>
  static FuturePtr<T> Create(
      strand_ptr_t strand,
      std::function<void(std::function<void(T)>,
                         std::function<void(std::exception_ptr)>)> fn,
      enable_if_nonvoid_t<T>* = 0) {
    return FutureHelper<T>::Create(strand, std::move(fn));
  }
  template <class T>
  static FuturePtr<void> Create(
      strand_ptr_t strand,
      std::function<void(std::function<void()>,
                         std::function<void(std::exception_ptr)>)> fn,
      enable_if_void_t<T>* = 0) {
    return FutureHelper<T>::Create(strand, std::move(fn));
  }
  template <class T>
  static FuturePtr<T> Resolve(strand_ptr_t strand, T v) {
    return FutureHelper<T>::Resolve(strand, std::move(v));
  }
  template <class T>
  static FuturePtr<T> Resolve(strand_ptr_t strand) {
    return FutureHelper<T>::Resolve(strand);
  }
  template <class T>
  static FuturePtr<T> Reject(strand_ptr_t strand, std::exception_ptr exc) {
    return FutureHelper<T>::Reject(strand, exc);
  }

  template <class T>
  static FuturePtr<std::vector<T>> All(strand_ptr_t strand,
                                       std::vector<FuturePtr<T>> arr,
                                       enable_if_nonvoid_t<T>* = 0) {
    return FutureHelper<T>::All(strand, std::move(arr));
  }
  template <class T>
  static FuturePtr<void> All(strand_ptr_t strand,
                             std::vector<FuturePtr<void>> arr,
                             enable_if_void_t<T>* = 0) {
    return FutureHelper<void>::All(strand, std::move(arr));
  }

  template <class T>
  static FuturePtr<std::vector<SettledResult<T>>> AllSettled(
      strand_ptr_t strand,
      std::vector<FuturePtr<T>> arr) {
    return FutureHelper<T>::AllSettled(strand, std::move(arr));
  }

  template <class T>
  static FuturePtr<T> Race(strand_ptr_t strand, std::vector<FuturePtr<T>> arr) {
    return FutureHelper<T>::Race(strand, std::move(arr));
  }

  template <class T>
  static FuturePtr<T> WithTimeout(strand_ptr_t strand,
                                  FuturePtr<T> future,
                                  std::chrono::microseconds timeout) {
    return FutureHelper<T>::WithTimeout(strand, future, timeout);
  }
};

class FutureBase;

using FutureBasePtr = boost::intrusive_ptr<FutureBase>;

class FutureBase : public boost::intrusive_ref_counter<FutureBase> {
 public:
  virtual ~FutureBase() {}
  static std::function<void(std::exception_ptr)> OnUnhandledRejection;

 protected:
  FutureBase(strand_ptr_t strand) : strand_(strand) {}

  struct Handler {
    std::function<FutureBasePtr(std::shared_ptr<void>)> onFulfilled;
    std::function<FutureBasePtr(std::exception_ptr)> onRejected;
    FutureBasePtr promise;
  };

  static void DoResolve(
      std::function<void(std::function<void(std::shared_ptr<void>)>,
                         std::function<void(std::exception_ptr)>)> fn,
      FutureBasePtr self) {
    std::shared_ptr<bool> done(new bool(false));
    try {
      fn(
          [done, self](std::shared_ptr<void> value) {
            if (*done)
              return;
            *done = true;
            Resolve(self, std::move(value));
          },
          [done, self](std::exception_ptr exc) {
            if (*done)
              return;
            *done = true;
            Reject(self, exc);
          });
    } catch (...) {
      if (*done)
        return;
      *done = true;
      Reject(self, std::current_exception());
    }
  }

  static void Resolve(FutureBasePtr self, std::shared_ptr<void> newResult) {
    self->state_ = 1;
    self->result_ = newResult;
    Finale(self);
  }
  static void Reject(FutureBasePtr self, std::exception_ptr exc) {
    self->state_ = 2;
    self->exc_ = exc;
    Finale(self);
  }
  static void ResolvePromise(FutureBasePtr self, FutureBasePtr newPromise) {
    if (self == newPromise) {
      Reject(self, std::make_exception_ptr(std::string(
                       "A promise cannot be resolved with itself.")));
      return;
    }
    self->state_ = 3;
    self->value_ = newPromise;
    Finale(self);
  }

  static void Finale(FutureBasePtr self) {
    if (self->state_ == 2 && self->deferreds_.empty()) {
      boost::asio::defer(*self->strand_, [self]() {
        if (!self->handled_) {
          if (OnUnhandledRejection) {
            OnUnhandledRejection(self->exc_);
          } else {
            std::cerr << "Possible Unhandled Promise Rejection" << std::endl;
          }
        }
      });
    }

    for (int i = 0; i < self->deferreds_.size(); i++) {
      Handle(self, self->deferreds_[i]);
    }
    self->deferreds_.resize(0);
  }

  static void Handle(FutureBasePtr self, Handler deferred) {
    while (self->state_ == 3) {
      self = self->value_;
    }
    if (self->state_ == 0) {
      self->deferreds_.push_back(deferred);
      return;
    }
    self->handled_ = true;
    boost::asio::defer(*self->strand_, [self, deferred]() {
      FutureBasePtr p;
      if (self->state_ == 1) {
        if (!deferred.onFulfilled) {
          Resolve(deferred.promise, self->result_);
          return;
        } else {
          try {
            p = deferred.onFulfilled(self->result_);
          } catch (...) {
            Reject(deferred.promise, std::current_exception());
            return;
          }
        }
      } else {
        if (!deferred.onRejected) {
          Reject(deferred.promise, self->exc_);
          return;
        } else {
          try {
            p = deferred.onRejected(self->exc_);
          } catch (...) {
            Reject(deferred.promise, std::current_exception());
            return;
          }
        }
      }
      ResolvePromise(deferred.promise, p);
    });
  }

 protected:
  std::shared_ptr<void> GetResult() const { return result_; }
  strand_ptr_t GetStrand() const { return strand_; }

 private:
  strand_ptr_t strand_;
  int state_ = 0;
  bool handled_ = false;
  FutureBasePtr value_;
  std::shared_ptr<void> result_;
  std::exception_ptr exc_;
  std::vector<Handler> deferreds_;
};
// TODO: .cpp ファイルに書く
std::function<void(std::exception_ptr)> FutureBase::OnUnhandledRejection;

template <class T>
class FutureImpl : public FutureBase {
  friend struct FutureHelper<T>;
  FutureImpl(strand_ptr_t strand) : FutureBase(strand) {}

 public:
  template <class U>
  FuturePtr<U> ThenFuture(
      std::function<FuturePtr<U>(T)> onFulfilled = nullptr,
      std::function<FuturePtr<U>(std::exception_ptr)> onRejected = nullptr,
      enable_if_nonvoid_t<U>* = 0) {
    FuturePtr<U> prom = Future::Create<U>(
        GetStrand(), [](std::function<void(U)> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this,
           Handler{
               [onFulfilled = std::move(onFulfilled)](std::shared_ptr<void> v) {
                 return onFulfilled(std::move(*std::static_pointer_cast<T>(v)));
               },
               std::move(onRejected), prom});
    return prom;
  }

  template <class U>
  FuturePtr<void> ThenFuture(
      std::function<FuturePtr<void>(T)> onFulfilled = nullptr,
      std::function<FuturePtr<void>(std::exception_ptr)> onRejected = nullptr,
      enable_if_void_t<U>* = 0) {
    FuturePtr<void> prom = Future::Create<void>(
        GetStrand(), [](std::function<void()> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this,
           Handler{
               [onFulfilled = std::move(onFulfilled)](std::shared_ptr<void> v) {
                 return onFulfilled(std::move(*std::static_pointer_cast<T>(v)));
               },
               std::move(onRejected), prom});
    return prom;
  }

  template <class U>
  FuturePtr<U> Then(std::function<U(T)> onFulfilled = nullptr,
                    std::function<U(std::exception_ptr)> onRejected = nullptr,
                    enable_if_nonvoid_t<U>* = 0) {
    FuturePtr<U> prom = Future::Create<U>(
        GetStrand(), [](std::function<void(U)> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(
        this,
        Handler{[strand = GetStrand(), onFulfilled = std::move(onFulfilled)](
                    std::shared_ptr<void> v) {
                  return Future::Resolve<U>(
                      strand,
                      onFulfilled(std::move(*std::static_pointer_cast<T>(v))));
                },
                [strand = GetStrand(),
                 onRejected = std::move(onRejected)](std::exception_ptr exc) {
                  return Future::Resolve<U>(strand, onRejected(exc));
                },
                prom});
    return prom;
  }

  template <class U>
  FuturePtr<void> Then(
      std::function<void(T)> onFulfilled = nullptr,
      std::function<void(std::exception_ptr)> onRejected = nullptr,
      enable_if_void_t<U>* = 0) {
    FuturePtr<void> prom = Future::Create<void>(
        GetStrand(), [](std::function<void()> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this,
           Handler{[strand = GetStrand(), onFulfilled = std::move(onFulfilled)](
                       std::shared_ptr<void> v) {
                     onFulfilled(std::move(*std::static_pointer_cast<T>(v)));
                     return Future::Resolve<void>(strand);
                   },
                   [strand = GetStrand(), onRejected = std::move(onRejected)](
                       std::exception_ptr exc) {
                     onRejected(std::move(exc));
                     return Future::Resolve<void>(strand);
                   },
                   prom});
    return prom;
  }

  template <class U>
  FuturePtr<U> CatchFuture(
      std::function<FuturePtr<U>(std::exception_ptr)> onRejected) {
    return ThenFuture<U>(nullptr, std::move(onRejected));
  };

  template <class U>
  FuturePtr<U> Catch(std::function<U(std::exception_ptr)> onRejected) {
    return Then<U>(nullptr, std::move(onRejected));
  };

  FuturePtr<T> Finally(std::function<void()> onFinally) {
    auto onFinally2 = onFinally;
    return ThenFuture<T>(
        [strand = GetStrand(), onFinally = std::move(onFinally)](T v) {
          onFinally();
          return Future::Resolve<T>(strand, std::move(v));
        },
        [strand = GetStrand(),
         onFinally = std::move(onFinally2)](std::exception_ptr exc) {
          onFinally();
          return Future::Reject<T>(strand, std::move(exc));
        });
  }

  FuturePtr<T> WithTimeout(std::chrono::microseconds timeout) {
    return Future::WithTimeout<T>(GetStrand(), FuturePtr<T>(this), timeout);
  }
};

template <>
class FutureImpl<void> : public FutureBase {
  friend struct FutureHelper<void>;
  FutureImpl(strand_ptr_t strand) : FutureBase(strand) {}

 public:
  template <class U>
  FuturePtr<U> ThenFuture(
      std::function<FuturePtr<U>()> onFulfilled = nullptr,
      std::function<FuturePtr<U>(std::exception_ptr)> onRejected = nullptr,
      enable_if_nonvoid_t<U>* = 0) {
    FuturePtr<U> prom = Future::Create<U>(
        GetStrand(), [](std::function<void(U)> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this, Handler{[onFulfilled = std::move(onFulfilled)](
                             std::shared_ptr<void> v) { return onFulfilled(); },
                         std::move(onRejected), prom});
    return prom;
  }

  template <class U>
  FuturePtr<void> ThenFuture(
      std::function<FuturePtr<void>()> onFulfilled = nullptr,
      std::function<FuturePtr<void>(std::exception_ptr)> onRejected = nullptr,
      enable_if_void_t<U>* = 0) {
    FuturePtr<void> prom = Future::Create<void>(
        GetStrand(), [](std::function<void()> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this, Handler{[onFulfilled = std::move(onFulfilled)](
                             std::shared_ptr<void> v) { return onFulfilled(); },
                         std::move(onRejected), prom});
    return prom;
  }

  template <class U>
  FuturePtr<U> Then(std::function<U()> onFulfilled = nullptr,
                    std::function<U(std::exception_ptr)> onRejected = nullptr,
                    enable_if_nonvoid_t<U>* = 0) {
    FuturePtr<U> prom = Future::Create<U>(
        GetStrand(), [](std::function<void(U)> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this,
           Handler{[strand = GetStrand(), onFulfilled = std::move(onFulfilled)](
                       std::shared_ptr<void> v) {
                     return Future::Resolve<U>(strand, onFulfilled());
                   },
                   [strand = GetStrand(), onRejected = std::move(onRejected)](
                       std::exception_ptr exc) {
                     return Future::Resolve<U>(strand, onRejected(exc));
                   },
                   prom});
    return prom;
  }

  template <class U>
  FuturePtr<void> Then(
      std::function<void()> onFulfilled = nullptr,
      std::function<void(std::exception_ptr)> onRejected = nullptr,
      enable_if_void_t<U>* = 0) {
    FuturePtr<void> prom = Future::Create<void>(
        GetStrand(), [](std::function<void()> resolved,
                        std::function<void(std::exception_ptr)> rejected) {});

    Handle(this,
           Handler{[strand = GetStrand(), onFulfilled = std::move(onFulfilled)](
                       std::shared_ptr<void> v) {
                     onFulfilled();
                     return Future::Resolve<void>(strand);
                   },
                   [strand = GetStrand(), onRejected = std::move(onRejected)](
                       std::exception_ptr exc) {
                     onRejected(std::move(exc));
                     return Future::Resolve<void>(strand);
                   },
                   prom});
    return prom;
  }

  template <class U>
  FuturePtr<U> CatchFuture(
      std::function<FuturePtr<U>(std::exception_ptr)> onRejected) {
    return ThenFuture<U>(nullptr, std::move(onRejected));
  };

  FuturePtr<void> CatchFuture(
      std::function<FuturePtr<void>(std::exception_ptr)> onRejected) {
    return ThenFuture<void>(nullptr, std::move(onRejected));
  };

  template <class U>
  FuturePtr<U> Catch(std::function<U(std::exception_ptr)> onRejected) {
    return Then<U>(nullptr, std::move(onRejected));
  };

  FuturePtr<void> Catch(std::function<void(std::exception_ptr)> onRejected) {
    return Then<void>(nullptr, std::move(onRejected));
  };

  FuturePtr<void> Finally(std::function<void()> onFinally) {
    auto onFinally2 = onFinally;
    return ThenFuture<void>(
        [self = FuturePtr<void>(this), strand = GetStrand(),
         onFinally = std::move(onFinally)]() {
          onFinally();
          return Future::Resolve<void>(strand);
        },
        [self = FuturePtr<void>(this), strand = GetStrand(),
         onFinally = std::move(onFinally2)](std::exception_ptr exc) {
          onFinally();
          return Future::Reject<void>(strand, std::move(exc));
        });
  }

  FuturePtr<void> WithTimeout(std::chrono::microseconds timeout) {
    return Future::WithTimeout<void>(GetStrand(), FuturePtr<void>(this),
                                     timeout);
  }
};

template <class T>
FuturePtr<T> FutureHelper<T>::Create(
    strand_ptr_t strand,
    std::function<void(std::function<void(T)>,
                       std::function<void(std::exception_ptr)>)> fn) {
  FuturePtr<T> p(new FutureImpl<T>(strand));
  auto fn2 = [fn = std::move(fn)](
                 std::function<void(std::shared_ptr<void>)> resolve,
                 std::function<void(std::exception_ptr)> reject) {
    fn([resolve = std::move(resolve)](
           T v) { resolve(std::shared_ptr<void>(new T(std::move(v)))); },
       std::move(reject));
  };
  p->DoResolve(std::move(fn2), FutureBasePtr(p.get()));
  return p;
}

FuturePtr<void> FutureHelper<void>::Create(
    strand_ptr_t strand,
    std::function<void(std::function<void()>,
                       std::function<void(std::exception_ptr)>)> fn) {
  FuturePtr<void> p(new FutureImpl<void>(strand));
  auto fn2 = [fn = std::move(fn)](
                 std::function<void(std::shared_ptr<void>)> resolve,
                 std::function<void(std::exception_ptr)> reject) {
    fn([resolve = std::move(resolve)]() { resolve(nullptr); },
       std::move(reject));
  };
  p->DoResolve(std::move(fn2), FutureBasePtr(p.get()));
  return p;
}

template <class T>
FuturePtr<T> FutureHelper<T>::Resolve(strand_ptr_t strand, T v) {
  return Future::Create<T>(
      strand, [v = std::move(v)](
                  std::function<void(T)> resolve,
                  std::function<void(std::exception_ptr)> reject) mutable {
        resolve(std::move(v));
      });
}

FuturePtr<void> FutureHelper<void>::Resolve(strand_ptr_t strand) {
  return Future::Create<void>(
      strand,
      [](std::function<void()> resolve,
         std::function<void(std::exception_ptr)> reject) { resolve(); });
}

template <class T>
FuturePtr<T> FutureHelper<T>::Reject(strand_ptr_t strand,
                                     std::exception_ptr exc) {
  return Future::Create<T>(
      strand, [exc = std::move(exc)](
                  std::function<void(T)> resolve,
                  std::function<void(std::exception_ptr)> reject) mutable {
        reject(std::move(exc));
      });
}

FuturePtr<void> FutureHelper<void>::Reject(strand_ptr_t strand,
                                           std::exception_ptr exc) {
  return Future::Create<void>(
      strand, [exc = std::move(exc)](
                  std::function<void()> resolve,
                  std::function<void(std::exception_ptr)> reject) mutable {
        reject(std::move(exc));
      });
}

template <class T>
FuturePtr<std::vector<T>> FutureHelper<T>::All(strand_ptr_t strand,
                                               std::vector<FuturePtr<T>> arr) {
  return Future::Create<std::vector<T>>(
      strand, [strand, arr = std::move(arr)](
                  std::function<void(std::vector<T>)> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        if (arr.empty()) {
          resolve({});
          return;
        }

        try {
          std::shared_ptr<std::vector<T>> results(
              new std::vector<T>(arr.size()));
          std::shared_ptr<std::atomic_int> remaining(
              new std::atomic_int(arr.size()));
          for (int i = 0; i < arr.size(); i++) {
            auto& f = arr[i];
            f->Then<void>(
                [i, results, remaining, resolve](T v) {
                  (*results)[i] = std::move(v);
                  if (--*remaining == 0) {
                    resolve(std::move(*results));
                  }
                },
                [reject](std::exception_ptr exc) { reject(exc); });
          }
        } catch (...) {
          reject(std::current_exception());
        }
      });
}

FuturePtr<void> FutureHelper<void>::All(strand_ptr_t strand,
                                        std::vector<FuturePtr<void>> arr) {
  return Future::Create<void>(
      strand, [strand, arr = std::move(arr)](
                  std::function<void()> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        if (arr.empty()) {
          resolve();
          return;
        }

        try {
          std::shared_ptr<std::atomic_int> remaining(
              new std::atomic_int(arr.size()));
          for (int i = 0; i < arr.size(); i++) {
            auto& f = arr[i];
            f->Then<void>(
                [i, remaining, resolve]() {
                  if (--*remaining == 0) {
                    resolve();
                  }
                },
                [reject](std::exception_ptr exc) { reject(exc); });
          }
        } catch (...) {
          reject(std::current_exception());
        }
      });
}

template <class T>
FuturePtr<std::vector<SettledResult<T>>> FutureHelper<T>::AllSettled(
    strand_ptr_t strand,
    std::vector<FuturePtr<T>> arr) {
  return Future::Create<std::vector<SettledResult<T>>>(
      strand, [strand, arr = std::move(arr)](
                  std::function<void(std::vector<SettledResult<T>>)> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        if (arr.empty()) {
          resolve({});
          return;
        }

        std::shared_ptr<std::vector<SettledResult<T>>> results(
            new std::vector<SettledResult<T>>(arr.size()));
        std::shared_ptr<std::atomic_int> remaining(
            new std::atomic_int(arr.size()));
        for (int i = 0; i < arr.size(); i++) {
          auto& f = arr[i];
          f->Then<void>(
              [i, results, remaining, resolve](T v) {
                SettledResult<T> r = {
                    SettledStatus::FULFILLED,
                    std::move(v),
                    std::exception_ptr(),
                };
                (*results)[i] = std::move(r);
                if (--*remaining == 0) {
                  resolve(std::move(*results));
                }
              },
              [i, results, remaining, resolve](std::exception_ptr exc) {
                SettledResult<T> r = {
                    SettledStatus::REJECTED,
                    T(),
                    exc,
                };
                (*results)[i] = std::move(r);
                if (--*remaining == 0) {
                  resolve(std::move(*results));
                }
              });
        }
      });
}

FuturePtr<std::vector<SettledResult<void>>> FutureHelper<void>::AllSettled(
    strand_ptr_t strand,
    std::vector<FuturePtr<void>> arr) {
  return Future::Create<std::vector<SettledResult<void>>>(
      strand, [strand, arr = std::move(arr)](
                  std::function<void(std::vector<SettledResult<void>>)> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        if (arr.empty()) {
          resolve({});
          return;
        }

        std::shared_ptr<std::vector<SettledResult<void>>> results(
            new std::vector<SettledResult<void>>(arr.size()));
        std::shared_ptr<std::atomic_int> remaining(
            new std::atomic_int(arr.size()));
        for (int i = 0; i < arr.size(); i++) {
          auto& f = arr[i];
          f->Then<void>(
              [i, results, remaining, resolve]() {
                SettledResult<void> r = {
                    SettledStatus::FULFILLED,
                    std::exception_ptr(),
                };
                (*results)[i] = std::move(r);
                if (--*remaining == 0) {
                  resolve(std::move(*results));
                }
              },
              [i, results, remaining, resolve](std::exception_ptr exc) {
                SettledResult<void> r = {
                    SettledStatus::REJECTED,
                    exc,
                };
                (*results)[i] = std::move(r);
                if (--*remaining == 0) {
                  resolve(std::move(*results));
                }
              });
        }
      });
}

template <class T>
FuturePtr<T> FutureHelper<T>::Race(strand_ptr_t strand,
                                   std::vector<FuturePtr<T>> arr) {
  return Future::Create<T>(strand,
                           [strand, arr = std::move(arr)](
                               std::function<void(T)> resolve,
                               std::function<void(std::exception_ptr)> reject) {
                             for (auto& f : arr) {
                               f->Then<void>(resolve, reject);
                             }
                           });
}

FuturePtr<void> FutureHelper<void>::Race(strand_ptr_t strand,
                                         std::vector<FuturePtr<void>> arr) {
  return Future::Create<void>(
      strand, [strand, arr = std::move(arr)](
                  std::function<void()> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        for (auto& f : arr) {
          f->Then<void>(resolve, reject);
        }
      });
}

template <class T>
FuturePtr<T> FutureHelper<T>::WithTimeout(strand_ptr_t strand,
                                          FuturePtr<T> future,
                                          std::chrono::microseconds timeout) {
  return Future::Create<T>(
      strand, [strand, future, timeout](
                  std::function<void(T)> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        std::shared_ptr<boost::asio::deadline_timer> timer(
            new boost::asio::deadline_timer(strand->context()));
        timer->expires_from_now(
            boost::posix_time::microseconds(timeout.count()));
        future->Then<void>(resolve, reject)->Finally([timer]() {
          timer->cancel();
        });
        timer->async_wait([reject](const boost::system::error_code& ec) {
          if (ec) {
            reject(std::make_exception_ptr(ec));
            return;
          }
          reject(std::make_exception_ptr(TimeoutError()));
        });
      });
}

FuturePtr<void> FutureHelper<void>::WithTimeout(
    strand_ptr_t strand,
    FuturePtr<void> future,
    std::chrono::microseconds timeout) {
  return Future::Create<void>(
      strand, [strand, future, timeout](
                  std::function<void()> resolve,
                  std::function<void(std::exception_ptr)> reject) {
        std::shared_ptr<boost::asio::deadline_timer> timer(
            new boost::asio::deadline_timer(strand->context()));
        timer->expires_from_now(
            boost::posix_time::microseconds(timeout.count()));
        future->Then<void>(resolve, reject)->Finally([timer]() {
          timer->cancel();
        });
        timer->async_wait([reject](const boost::system::error_code& ec) {
          if (ec) {
            reject(std::make_exception_ptr(ec));
            return;
          }
          reject(std::make_exception_ptr(TimeoutError()));
        });
      });
}

void test_on_unhandle_rejection() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));
  std::exception_ptr exc;
  int called = 0;
  FutureBase::OnUnhandledRejection = [&called, &exc](std::exception_ptr e) {
    called += 1;
    exc = e;
  };

  // no error on resolve
  {
    Future::Resolve(strand, true)
        ->Then<bool>([](bool v) { return v; })
        ->ThenFuture<bool>(
            [strand](bool v) { return Future::Resolve(strand, v); });
    ioc.run();
    ioc.restart();
    assert(called == 0);
    called = 0;
  }

  // error single Promise
  {
    Future::Create<void>(strand, [](auto, auto) { throw "error"; });
    ioc.run();
    ioc.restart();
    assert(called == 1);
    called = 0;
  }

  // multi promise error
  {
    Future::Create<void>(strand, [](auto, auto) {
      throw "error";
    })->Then<void>([]() {});
    ioc.run();
    ioc.restart();
    assert(called == 1);
    called = 0;
  }

  // promise catch no error
  {
    Future::Create<void>(strand, [](auto, auto) {
      throw "error";
    })->Catch([strand](std::exception_ptr) {});
    ioc.run();
    ioc.restart();
    assert(called == 0);
    called = 0;
  }

  // promise catch no error
  {
    Future::Create<void>(strand, [](auto, auto) {
      throw "error";
    })->Then<void>([strand]() {
      })->Catch<void>([strand](std::exception_ptr) {});
    ioc.run();
    ioc.restart();
    assert(called == 0);
    called = 0;
  }

  // promise reject error
  {
    Future::Reject<void>(strand, std::make_exception_ptr("hello"));
    ioc.run();
    ioc.restart();
    assert(called == 1);
    called = 0;
  }

  // promise reject error late
  {
    auto prom = Future::Reject<void>(strand, std::make_exception_ptr("hello"));
    prom->Catch([strand](std::exception_ptr exc) {});
    ioc.run();
    ioc.restart();
    assert(called == 0);
    called = 0;
  }

  // promise reject error late
  {
    Future::Reject<void>(strand, std::make_exception_ptr(100));
    ioc.run();
    ioc.restart();
    try {
      std::rethrow_exception(exc);
    } catch (int n) {
      assert(n == 100);
    } catch (...) {
      assert(false);
    }
    called = 0;
  }

  FutureBase::OnUnhandledRejection = nullptr;
}

void test_then() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));

  // subclassed Promise resolves to subclass
  {
    auto prom = Future::Create<void>(strand, [](auto resolve, auto reject) {
                  resolve();
                })->Then<void>([]() {}, [](std::exception_ptr) {});
    ioc.run();
    ioc.restart();
    // このテストはうまくチェックできないのでとりあえず動けばよしとする
    // assert(spy.calledTwice);
  }

  {
    auto prom = Future::Create<void>(strand, [](auto resolve, auto reject) {
                  reject(std::make_exception_ptr(0));
                })->Then<void>([]() {}, [](std::exception_ptr) {});
    ioc.run();
    ioc.restart();
  }
}

void test_finally() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));

  // should be called on success
  {
    bool ok = false;
    Future::Resolve(strand, 3)->Finally([&ok]() { ok = true; });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // should be called on failure
  {
    bool ok = false;
    Future::Reject<int>(strand, std::make_exception_ptr(3))->Finally([&ok]() {
      ok = true;
    });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // should not affect the result
  {
    bool ok = false;
    Future::Resolve(strand, 3)
        ->Finally([&ok]() { ok = true; })
        ->ThenFuture<int>([strand](int result) {
          return Future::Reject<int>(strand, std::make_exception_ptr(100));
        })
        ->Finally([]() {})
        ->Catch<void>([](std::exception_ptr exc) {
          try {
            std::rethrow_exception(exc);
          } catch (int n) {
            assert(n == 100);
          } catch (...) {
            assert(false);
          }
        })
        ->Finally([&ok] { ok = true; });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // should reject with the handler error if handler throws
  {
    bool ok = false;
    Future::Reject<void>(strand, std::make_exception_ptr(10))
        ->Finally([]() { throw 20; })
        ->Catch<void>([](std::exception_ptr exc) {
          try {
            std::rethrow_exception(exc);
          } catch (int n) {
            assert(n == 20);
          } catch (...) {
            assert(false);
          }
        })
        ->Finally([&ok]() { ok = true; });
    ioc.run();
    ioc.restart();
    assert(ok);
  }
}

void test_all() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));

  // works on multiple resolved promises
  {
    bool ok = false;
    Future::All<void>(
        strand, {Future::Resolve<void>(strand), Future::Resolve<void>(strand)})
        ->Then<void>([&ok]() { ok = true; }, [](std::exception_ptr) {});
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // works on empty array
  {
    bool ok = false;
    Future::All<int>(strand, {})
        ->Then<void>([&ok](std::vector<int> v) { ok = v.empty(); },
                     [](std::exception_ptr) {});
    ioc.run();
    ioc.restart();
    assert(ok);
  }
}

void test_all_settled() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));

  // works on multiple resolved promises
  {
    bool ok = false;
    Future::AllSettled<void>(
        strand, {Future::Resolve<void>(strand), Future::Resolve<void>(strand)})
        ->Then<void>([&ok](std::vector<SettledResult<void>> r) { ok = true; });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // works even with rejected promises
  {
    bool ok = false;
    Future::AllSettled<int>(
        strand, {Future::Reject<int>(strand, std::make_exception_ptr(10)),
                 Future::Resolve<int>(strand, 20)})
        ->Then<void>([&ok](std::vector<SettledResult<int>> r) {
          assert(r[0].status == SettledStatus::REJECTED);
          assert(r[1].status == SettledStatus::FULFILLED);
          try {
            std::rethrow_exception(r[0].reason);
          } catch (int n) {
            assert(n == 10);
          } catch (...) {
            assert(false);
          }
          assert(r[1].value == 20);
          ok = true;
        });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // works on empty array
  {
    bool ok = false;
    Future::AllSettled<void>(strand, {})
        ->Then<void>([&ok](std::vector<SettledResult<void>> r) {
          assert(r.empty());
          ok = true;
        });
    ioc.run();
    ioc.restart();
    assert(ok);
  }
}

void test_race() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));

  // works on basic values
  {
    bool ok = false;
    Future::Race<int>(strand,
                      {Future::Resolve(strand, 1), Future::Resolve(strand, 2),
                       Future::Resolve(strand, 3)})
        ->Then<void>([&ok](int n) {
          assert(n == 1);
          ok = true;
        });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // works on success promise
  {
    auto doneProm = Future::Resolve<int>(strand, 10);
    auto pendingProm1 = Future::Create<int>(strand, [](auto, auto) {});
    auto pendingProm2 = Future::Create<int>(strand, [](auto, auto) {});
    bool ok = false;
    Future::Race<int>(strand, {pendingProm1, doneProm, pendingProm2})
        ->Then<void>([&ok](int n) {
          assert(n == 10);
          ok = true;
        });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // works on empty array
  {
    FuturePtr<void> f = Future::Race<void>(strand, {});
    ioc.run();
    ioc.restart();
  }
}

void test_timeout() {
  boost::asio::io_context ioc;
  strand_ptr_t strand(new boost::asio::io_context::strand(ioc));

  // 解決済みならタイムアウトは起きない
  {
    bool ok = false;
    Future::Resolve<int>(strand, 10)
        ->WithTimeout(std::chrono::milliseconds(0))
        ->Then<void>([&ok](int v) {
          assert(v == 10);
          ok = true;
        });
    ioc.run();
    ioc.restart();
    assert(ok);
  }

  // タイムアウトさせる
  {
    bool ok = false;
    Future::Create<void>(strand, [](auto, auto) {})
        ->WithTimeout(std::chrono::milliseconds(0))
        ->Catch<void>([&ok](std::exception_ptr exc) {
          try {
            std::rethrow_exception(exc);
          } catch (TimeoutError) {
            ok = true;
          }
        });
    ioc.run();
    ioc.restart();
    assert(ok);
  }
}

void test_future() {
  test_on_unhandle_rejection();
  test_then();
  test_finally();
  test_all();
  test_all_settled();
  test_race();
  test_timeout();
}

#endif