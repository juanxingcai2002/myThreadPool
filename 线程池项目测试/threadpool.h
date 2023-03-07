#ifndef  THREADPOOL_H
#define  THREADPOOL_H

#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<unordered_map>




// Any类型：可以接收任意数据的类型
class Any
{
public:
	// 尽可能都要将默认的构造写出即使不用
	Any() = default;

	~Any() = default;

	// unique_ptr 禁用了左值拷贝构造和和左值赋值函数，但是运行右值拷贝构造。
	Any(const Any&) = delete;
	Any& operator=(Any&) = delete;

	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data))
	{}//可以用作隐式类型的转换，任意类型的T可以转换成Any对象。

	// 这个方法可以把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 从base_中找到它所指的Derive对象，从它里面取出data成员变量。
		// 基类指针 -- 》 转换成派生类指针
		Derive<T>* pd = dynamic_cast<Derive<T> *>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}

private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default;
	};


	// 派生类类型
	template<typename T>
	class Derive :public Base
	{
	public:
		Derive(T data) 
			:data_(data)
		{}
		T data_;
	};
private:
	std::unique_ptr<Base> base_;
};


// 手写信号量类
class Semaphere
{
public:
	Semaphere(int limit = 0) : resLimit_(limit){}
	~Semaphere() = default;
	// 消费一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		// 在有资源的情况下才可以消费
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// 增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		resLimit_++;
		cond_.notify_all();// 告诉其他线程信号量+1
	}



private:
	int resLimit_;// 资源计数
	std::mutex mtx_;
	std::condition_variable cond_;
};


// 实现 接收提交到线程池task任务执行完成后的返回值类型
// 任务是在工作线程中执行的，用户调用get（）方法获得返回值是另外的一个线程，需要先执行完，用户才能获得（设计到线程通信）--》信号量
class Task;
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isvalid = true);
	~Result() = default; // 写出来加default关键字有利于编译器优化	
	
	// 如何获取任务执行完（task-》run）的返回值any？，将run（）返回值作为参数传给any，用Reslut中的对象的属性保存起来。
	void setVal(Any any);


	// 用户调用get（）方法获取task的返回值。
	Any get();


private:
	Any any_;//存储任务的返回值
	Semaphere sem_;
	std::shared_ptr<Task>task_;//指向对于获取返回值的任务对象。获得submitTask中的参数对象。
	// 是为了 Result（task） 保证 Result对象中有东西 可以控制一个task对象，获取task对象 执行runc的返回值。
	std::atomic_bool isValid_;//判断任务task是否有效。
};




class Thread
{
public:
	using ThreadFunc = std::function<void(int)>; //定义线程函数对象类型。
	
	Thread(ThreadFunc func);
	~Thread();
	int getId()const;

	void start();
private:
	ThreadFunc func_; // 函数对象
	static int generateId_;
	int threadId_;// 保存线程id。
};


//任务类：具体的类型由用户提供，因此从设计者的角度出发，设计出的任务类型应该能接收用户传递过来的所有类型，具体的操作由不同的类型决定
//因此可以设计 基类为抽象基类 就可以接收用户的所有类型。

// 不能直接在Task类中直接包含Result类，因为一旦Task类的对象没了，相应的Resut类的对象也会被回收，包含指针即可。
class Task
{
public:
	void exec();
	void setResult(Result* res);
	Task();
	~Task() = default;
	virtual Any run() = 0;// 用户可以自定义任意任务类型，从task继承，重写run（） 方法，实现自定义的任务处理。
private:
	Result*  result_;// Result 对象的声明周期也应该大于继承Task而来的用户设计的对象。
};




// 线程支持的模式
enum  PoolMode
{
	MODE_FIXED,// 固定线程数量
	MODE_CACHED,// 线程数量可动态增长
};


//线程池类
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	
	// 设置线程池的工作模式
	void setMode(PoolMode mode);
	/*void setInitThreadSize(int size);*/

	// 设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	void setThreadSizeThreshHold(int threshhold);//设置线程池cache模式下线程阈值，因为用户的cpu数不定，因此，通过一个函数将线程的最大值交给用户设定。

	//给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);
	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());


	// 人为不希望拷贝和赋值,线程池对象的东西太多了，赋值起来等不安全消耗也大。
	ThreadPool(const ThreadPool& s) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数 ：原本应该放在线程类中，但是线程函数要访问的属性都在threadPool的private属性中，干脆就在ThreadPool类中定义线程的启动函数
	void threadFunc(int threadid);// 线程类中线程应该做的事情。

	// ThreadPool中的set函数只有在线程运行的时候才能运行，因此写一个判断线程是否正在运行的函数，因为不是给外界调用的接口，而是自己的成员函数中使用
	// 权限设置为private
	//检查线程池的状态
	bool checkRunningState()const;

private:
	/*std::vector<Thread*>threads_;*///线程列表。   // 存放线程的容器，只会往外给不用考虑线程安全问题。
	// 但是还是有问题，因为我们在创建线程对象的时候 采用了new 方法，因此需要手动释放内存，为了避免手动释放内存的烦扰---》采用智能指针
	
	/*std::vector<std::unique_ptr<Thread>>threads_;*/
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;//int --> 线程id 来标识线程容器中的每一个指向线程对象的指针。
	
	size_t initThreadSize_;// 初始的线程数量
	//size_t MaxThreadSize_;// 

	// 线程数量上限的阈值（cached模式）
	int threadSizeThreshHold_;
	// 记录当前线程池里面线程的总数量 ，因为会随时改变--》防止多线程的意外--》改用atomic_int类型
	std::atomic_int curThreadSize_;
	// 记录空闲线程的数量
	std::atomic_int idleThreadSize_;
	

	std::queue<std::shared_ptr<Task>>taskQue_;// 任务队列	
			// 必须用基类指针，因为只有基类的指针和引用才能接收子类对象。
			
	// 线程互斥问题
	// 线程消费一个任务，用户产生一个任务--》多线程问题--》线程安全，因为只有++，--不需要用锁---》用atomic即可。																		// 必须用智能指针：因为用户可能创建的对象是临时对象，如果临时对象被销毁，Task*指向一块被释放的内存会造成指针异常--->因此这里应该使用只能指针
	std::atomic_int taskSize_;//任务的数量
	int taskQueMaxThreshHold_;//任务队列数量上线阈值。

	// 线程通信（互斥锁 + 条件变量）
	std::mutex taskQueMtx_;//保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable notEmpty_;//表示任务队列不空
	std::condition_variable exitCond_;// 等待改线程资源全部回收

	PoolMode PoolMode_;//当前线程池的模式



	// 表示当前线程的启动状态，因为在多个线程中 都可能会用到，因此--设计成原子类型。
	 std::atomic_bool isPoolRunning_;
	 
};






#endif

