#include "threadpool.h"
#include<functional>
#include<iostream>
#include<memory>


const int TASK_MAX_THRESHHOLD = 1024;//项目中除了0，1其他的数字尽可能用具有意义的量表示。
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60;
//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(4)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, PoolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
	, idleThreadSize_(0)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, curThreadSize_(0)
{};

// 线程池析构 ：必须写出，即使什么也没干（因为构造过程中所有的变量都是在栈上创建由编译器管理释放）
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	notEmpty_.notify_all();//将等待状态的的线程由等待态 --- 》 阻塞态（准备获取锁）
	// 等待线程池里面所有的线程返回，线程有两种状态，阻塞或者正在执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; }); //函数对象返回fals的时候就阻塞在这
};


// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode) {
	if (isPoolRunning_) return;// 保证在线程启动之前就设置好，后面同理。
	PoolMode_ = mode;
}

//void ThreadPool::setInitThreadSize(int size)
//{
//	initThreadSize_ = size;
//}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)

{
	if (isPoolRunning_) return;
	taskQueMaxThreshHold_ = threshhold;
}

//设置线程队列的阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (isPoolRunning_) return;
	if (PoolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

//给线程池提交任务，用户调用该接口，传入任务对象。生产任务。（生产者）
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁：消费者对临界资源的访问应该是互斥的
	std::unique_lock<std::mutex>lock(taskQueMtx_);

	// 线程通信--》与工作线程（消费者）通信，等待任务队列有空余
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lock); //当前队列满了，不满足不满的条件，因此在该条件变量上等待。
	//}
	
	//需求：如果阻塞，不应该超过1s，否则应该返回。
	// 防治任务的堆积
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))//相当于上面的while的那段逻辑
	{
		// 表示等待了1s，条件依然不满足
		std::cerr << "task queue is full,submit task fail " << std::endl;
		//return sp->getResult;
		return Result(sp,false);
	}
	//如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	//因为放了新任务，任务队列肯定不空,在notEmpty()通知的是消费者。
	notEmpty_.notify_all();


	// cache模式：任务处理比较紧急，场景：小而快的任务，需要根据任务数量和空闲线程数量，判断是否需要创建线程，不能用于大任务
	// 大任务耗时久，长期占据线程，有更多的任务到来，就需要不断的创建线程，这样的开销是很大的
	if (PoolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
	{
		// 创建新线程 -- >创建线程对象 + 启动该线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();
		// 修改线程个数的相关变量
		curThreadSize_++;
		idleThreadSize_++;// 启动线程了但没有执行，因此属于空闲线程
	}

	// 返回任务的Result对象
	return Result(sp);
	/*return sp->getResult();*/
}
// 开启线程池
void ThreadPool::start(int initThreadSize) //在启动的过程中默认设置一个线程池中线程的数量，就可以合并设置初始线程的数量的函数
{
	// 设置线程池的运行状态。
	isPoolRunning_ = true;
	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象的同时把线程函数（threadFunc）给到线程对象 --》采用bind（）方法
	for (int i = 0; i < initThreadSize_; i++)
	{
		// 创建一个智能指针，指向一个函数对象。
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));//unordered_map 插入键值对的成员函数。
		/*threads_.emplace_back(std::move(ptr));*/
	}
	// 启动所有线程
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;// 只是启动，还没给线程提交任务，记录初始空闲线程的数量。
	}
	
}


// 定义线程函数：原本是线程类中应该实现的，这里由threadPool类实现，然后通过bind（）方法传递给线程类。
// 将线程池中的线程函数 用bind（）绑定器绑定成一个函数对象，在线程Thread类中用一个function类对象接收。
void ThreadPool::threadFunc(int threadid)
{
	// isPoolRunning 为true的时候，才表示线程池正常运行。
	while (isPoolRunning_)//线程池中的线程应该是一直执行着这个动作（从任务队列中取任务然后执行），因此应该放在死循环中
	{
		

		auto lastTime = std::chrono::high_resolution_clock().now();//


		std::shared_ptr<Task> task; // 先声明，用来接收从任务队列中取出的值
		{// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			
			
			// cached模式下，有可能已经创建了很大的线程，但是空闲时间超过了60s，应该把多余的线程（超过initThreadSize_数量的线程
			//）进行回收   当前时间 - 上一次线程执行的时间 > 60s
			
			
				// cached模式下是需要限制每一秒中返回一次来判断它的空闲时间，如何区分超时返回和有任务待执行返回。
			while (taskQue_.size() == 0)// 说明没有任务，工作线程就应该阻塞在这。
			{
				if (PoolMode_ == PoolMode::MODE_CACHED)
				{
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))// 阻塞1s就返回,且返回类型有两种，超时返回和非超时返回（说明有任务来了，给条件变量的值++了）
					{

						//记录当前的时间
						auto now = std::chrono::high_resolution_clock::now();
						// 计算差值，利用这个函数的时候要注意单位
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > 60 && curThreadSize_ > initThreadSize_)//线程回收也是有个度的 
						{
							// 开始回收线程
							/*1、记录线程数量的相关变量的值修改
							2、把线程对象从线程列表容器中删除，但是没有办法知道执行threadFucn的线程 与 thread容器中的对应的那个线程从容器中删除
							因此线需要给每个线程一个线程id --》来标识每个线程*/
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							return;
						}
					}
				}
				else
				{
					// fixed模式下是如果线程函数不能执行就一直等待条件变量直到可以执行。
					// 等待notEmpty；
					notEmpty_.wait(lock);
				}

				// 线程池要结束，回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);// 删除线程对象
					exitCond_.notify_all();//线程被删除了，唤醒一些等待再这个条件变量上的所有线程。
					return;
				}
				
			}
			
 
			// 说明线程启动。那么空闲的线程就少了
			idleThreadSize_--;
			// 从任务队列中取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果还有剩余任务，继续通知其他工作线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 取出任务，进行通知用户线程可以提交任务
			notFull_.notify_all();

			// 不应该在执行任务的时候带着锁，应该执行任务的之前就放开锁，让其他线程也获得锁 -- 》方法，利用作用域
		}

		// 当前线程负责执行这个任务
		if (task != nullptr)
		{
			/*task->run();*/
			task->exec();
		}
		idleThreadSize_++;//线程的任务已经执行完了，空闲的线程数++；
		lastTime = std::chrono::high_resolution_clock().now();// 更新线程执行完任务的时间
	}

	// 如果线程池回收的时候线程正在执行任务即再while（）循环中它进去的时候isPoolRunning == true，它还在执行的时候，isPoolRunning == false了
	//再次while 循环进入发现空了，它就会退出while 循环 --》 那么也应该将这个线程的从线程列表中删除那么才叫做真的线程回收
	threads_.erase(threadid);
	exitCond_.notify_all();
}

int ::Thread::generateId_ = 0;
// 线程启动状态的判定
bool ThreadPool::checkRunningState()const
{
	return isPoolRunning_;
}




// 线程构造
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++)
{};

Thread::~Thread() {};



// 线程的启动 == 》 创建一个线程对象--》执行一个线程函数 --》若任务队列中有任务，则去任务队列中执行任务。
void Thread::start()
{
	// 在thread类创建对象的时候，就确定了函数对象，然后通过创建线程去执行。
	// 
	// 创建一个线程来执行线程函数
	std::thread t(func_,threadId_);//c++11来说，线程对象t，和线程函数func_; 执行线程函数func_  == -> 执行threadPool 下的ThreadFunc
	t.detach();//将线程函数和线程对象的关系分开，生命周期不在具有限制。  ----》设置成分离线程。
}


int Thread::getId()const
{
	return threadId_;
}



// Task方法的实现


Task::Task()
	:result_(nullptr)
{}

void Task::exec() // 基类普通函数的调用
{
	if (result_ != nullptr)
	{
		/*	run();*/
		result_->setVal(run());// 在这里发生多态,线程函数执行run()，并将run方法得到的返回值 用过serResult（）方法用一个Result对象接收
	}
}

//用于初始化Task类中的Result对象。
void Task::setResult(Result* res)
{
	result_ = res;

}





// Result 方法的实现
Result::Result(std::shared_ptr<Task> task, bool isvalid) 
	:isValid_(isvalid), task_(task)
{
	task_->setResult(this);
	}


Any Result::get()
{
	if (!isValid_)// task的对象可能已经被回收了，get方法的返回值是依赖于task对象的，但是当然不能依赖一个被回收的对象
	{
		return "";
	}
	sem_.wait();// task任务线程如果每执行完，那么这里会阻塞用户线程调用get（）方法阻塞在这。
	return std::move(any_);// any_是一个unique_ptr,return 会反生左值拷贝构造，但unique-ptr禁止了，因此要转换成右值
}


void Result::setVal(Any any)
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post();
}