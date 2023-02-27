#include "threadpool.h"
#include<functional>
#include<iostream>
#include<memory>


const int TASK_MAX_THRESHHOLD = 1024;//��Ŀ�г���0��1���������־������þ������������ʾ��
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 60;
//�̳߳ع���
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

// �̳߳����� ������д������ʹʲôҲû�ɣ���Ϊ������������еı���������ջ�ϴ����ɱ����������ͷţ�
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	notEmpty_.notify_all();//���ȴ�״̬�ĵ��߳��ɵȴ�̬ --- �� ����̬��׼����ȡ����
	// �ȴ��̳߳��������е��̷߳��أ��߳�������״̬��������������ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; }); //�������󷵻�fals��ʱ�����������
};


// �����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode) {
	if (isPoolRunning_) return;// ��֤���߳�����֮ǰ�����úã�����ͬ��
	PoolMode_ = mode;
}

//void ThreadPool::setInitThreadSize(int size)
//{
//	initThreadSize_ = size;
//}

// ����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)

{
	if (isPoolRunning_) return;
	taskQueMaxThreshHold_ = threshhold;
}

//�����̶߳��е���ֵ
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (isPoolRunning_) return;
	if (PoolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

//���̳߳��ύ�����û����øýӿڣ�������������������񡣣������ߣ�
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ���������߶��ٽ���Դ�ķ���Ӧ���ǻ����
	std::unique_lock<std::mutex>lock(taskQueMtx_);

	// �߳�ͨ��--���빤���̣߳������ߣ�ͨ�ţ��ȴ���������п���
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lock); //��ǰ�������ˣ������㲻��������������ڸ����������ϵȴ���
	//}
	
	//���������������Ӧ�ó���1s������Ӧ�÷��ء�
	// ��������Ķѻ�
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))//�൱�������while���Ƕ��߼�
	{
		// ��ʾ�ȴ���1s��������Ȼ������
		std::cerr << "task queue is full,submit task fail " << std::endl;
		//return sp->getResult;
		return Result(sp,false);
	}
	//����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++;

	//��Ϊ����������������п϶�����,��notEmpty()֪ͨ���������ߡ�
	notEmpty_.notify_all();


	// cacheģʽ��������ȽϽ�����������С�����������Ҫ�������������Ϳ����߳��������ж��Ƿ���Ҫ�����̣߳��������ڴ�����
	// �������ʱ�ã�����ռ���̣߳��и����������������Ҫ���ϵĴ����̣߳������Ŀ����Ǻܴ��
	if (PoolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
	{
		// �������߳� -- >�����̶߳��� + �������̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();
		// �޸��̸߳�������ر���
		curThreadSize_++;
		idleThreadSize_++;// �����߳��˵�û��ִ�У�������ڿ����߳�
	}

	// ���������Result����
	return Result(sp);
	/*return sp->getResult();*/
}
// �����̳߳�
void ThreadPool::start(int initThreadSize) //�������Ĺ�����Ĭ������һ���̳߳����̵߳��������Ϳ��Ժϲ����ó�ʼ�̵߳������ĺ���
{
	// �����̳߳ص�����״̬��
	isPoolRunning_ = true;
	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳����ͬʱ���̺߳�����threadFunc�������̶߳��� --������bind��������
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ����һ������ָ�룬ָ��һ����������
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));//unordered_map �����ֵ�Եĳ�Ա������
		/*threads_.emplace_back(std::move(ptr));*/
	}
	// ���������߳�
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;// ֻ����������û���߳��ύ���񣬼�¼��ʼ�����̵߳�������
	}
	
}


// �����̺߳�����ԭ�����߳�����Ӧ��ʵ�ֵģ�������threadPool��ʵ�֣�Ȼ��ͨ��bind�����������ݸ��߳��ࡣ
// ���̳߳��е��̺߳��� ��bind���������󶨳�һ�������������߳�Thread������һ��function�������ա�
void ThreadPool::threadFunc(int threadid)
{
	// isPoolRunning Ϊtrue��ʱ�򣬲ű�ʾ�̳߳��������С�
	while (isPoolRunning_)//�̳߳��е��߳�Ӧ����һֱִ������������������������ȡ����Ȼ��ִ�У������Ӧ�÷�����ѭ����
	{
		

		auto lastTime = std::chrono::high_resolution_clock().now();//


		std::shared_ptr<Task> task; // ���������������մ����������ȡ����ֵ
		{// ��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			
			
			// cachedģʽ�£��п����Ѿ������˺ܴ���̣߳����ǿ���ʱ�䳬����60s��Ӧ�ðѶ�����̣߳�����initThreadSize_�������߳�
			//�����л���   ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
			
			
				// cachedģʽ������Ҫ����ÿһ���з���һ�����ж����Ŀ���ʱ�䣬������ֳ�ʱ���غ��������ִ�з��ء�
			while (taskQue_.size() == 0)// ˵��û�����񣬹����߳̾�Ӧ���������⡣
			{
				if (PoolMode_ == PoolMode::MODE_CACHED)
				{
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))// ����1s�ͷ���,�ҷ������������֣���ʱ���غͷǳ�ʱ���أ�˵�����������ˣ�������������ֵ++�ˣ�
					{

						//��¼��ǰ��ʱ��
						auto now = std::chrono::high_resolution_clock::now();
						// �����ֵ���������������ʱ��Ҫע�ⵥλ
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > 60 && curThreadSize_ > initThreadSize_)//�̻߳���Ҳ���и��ȵ� 
						{
							// ��ʼ�����߳�
							/*1����¼�߳���������ر�����ֵ�޸�
							2�����̶߳�����߳��б�������ɾ��������û�а취֪��ִ��threadFucn���߳� �� thread�����еĶ�Ӧ���Ǹ��̴߳�������ɾ��
							�������Ҫ��ÿ���߳�һ���߳�id --������ʶÿ���߳�*/
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							return;
						}
					}
				}
				else
				{
					// fixedģʽ��������̺߳�������ִ�о�һֱ�ȴ���������ֱ������ִ�С�
					// �ȴ�notEmpty��
					notEmpty_.wait(lock);
				}

				// �̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);// ɾ���̶߳���
					exitCond_.notify_all();//�̱߳�ɾ���ˣ�����һЩ�ȴ���������������ϵ������̡߳�
					return;
				}
				
			}
			
 
			// ˵���߳���������ô���е��߳̾�����
			idleThreadSize_--;
			// �����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �������ʣ�����񣬼���֪ͨ���������߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ�����񣬽���֪ͨ�û��߳̿����ύ����
			notFull_.notify_all();

			// ��Ӧ����ִ�������ʱ���������Ӧ��ִ�������֮ǰ�ͷſ������������߳�Ҳ����� -- ������������������
		}

		// ��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			/*task->run();*/
			task->exec();
		}
		idleThreadSize_++;//�̵߳������Ѿ�ִ�����ˣ����е��߳���++��
		lastTime = std::chrono::high_resolution_clock().now();// �����߳�ִ���������ʱ��
	}

	// ����̳߳ػ��յ�ʱ���߳�����ִ��������while����ѭ��������ȥ��ʱ��isPoolRunning == true��������ִ�е�ʱ��isPoolRunning == false��
	//�ٴ�while ѭ�����뷢�ֿ��ˣ����ͻ��˳�while ѭ�� --�� ��ôҲӦ�ý�����̵߳Ĵ��߳��б���ɾ����ô�Ž�������̻߳���
	threads_.erase(threadid);
	exitCond_.notify_all();
}

int ::Thread::generateId_ = 0;
// �߳�����״̬���ж�
bool ThreadPool::checkRunningState()const
{
	return isPoolRunning_;
}




// �̹߳���
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++)
{};

Thread::~Thread() {};



// �̵߳����� == �� ����һ���̶߳���--��ִ��һ���̺߳��� --���������������������ȥ���������ִ������
void Thread::start()
{
	// ��thread�ഴ�������ʱ�򣬾�ȷ���˺�������Ȼ��ͨ�������߳�ȥִ�С�
	// 
	// ����һ���߳���ִ���̺߳���
	std::thread t(func_,threadId_);//c++11��˵���̶߳���t�����̺߳���func_; ִ���̺߳���func_  == -> ִ��threadPool �µ�ThreadFunc
	t.detach();//���̺߳������̶߳���Ĺ�ϵ�ֿ����������ڲ��ھ������ơ�  ----�����óɷ����̡߳�
}


int Thread::getId()const
{
	return threadId_;
}



// Task������ʵ��


Task::Task()
	:result_(nullptr)
{}

void Task::exec() // ������ͨ�����ĵ���
{
	if (result_ != nullptr)
	{
		/*	run();*/
		result_->setVal(run());// �����﷢����̬,�̺߳���ִ��run()������run�����õ��ķ���ֵ �ù�serResult����������һ��Result�������
	}
}

//���ڳ�ʼ��Task���е�Result����
void Task::setResult(Result* res)
{
	result_ = res;

}





// Result ������ʵ��
Result::Result(std::shared_ptr<Task> task, bool isvalid) 
	:isValid_(isvalid), task_(task)
{
	task_->setResult(this);
	}


Any Result::get()
{
	if (!isValid_)// task�Ķ�������Ѿ��������ˣ�get�����ķ���ֵ��������task����ģ����ǵ�Ȼ��������һ�������յĶ���
	{
		return "";
	}
	sem_.wait();// task�����߳����ÿִ���꣬��ô����������û��̵߳���get���������������⡣
	return std::move(any_);// any_��һ��unique_ptr,return �ᷴ����ֵ�������죬��unique-ptr��ֹ�ˣ����Ҫת������ֵ
}


void Result::setVal(Any any)
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();
}