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




// Any���ͣ����Խ����������ݵ�����
class Any
{
public:
	// �����ܶ�Ҫ��Ĭ�ϵĹ���д����ʹ����
	Any() = default;

	~Any() = default;

	// unique_ptr ��������ֵ��������ͺ���ֵ��ֵ����������������ֵ�������졣
	Any(const Any&) = delete;
	Any& operator=(Any&) = delete;

	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data))
	{}//����������ʽ���͵�ת�����������͵�T����ת����Any����

	// ����������԰�Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// ��base_���ҵ�����ָ��Derive���󣬴�������ȡ��data��Ա������
		// ����ָ�� -- �� ת����������ָ��
		Derive<T>* pd = dynamic_cast<Derive<T> *>(base_.get());
		if (pd == nullptr)
		{
			throw "type is unmatch";
		}
		return pd->data_;
	}

private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;
	};


	// ����������
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


// ��д�ź�����
class Semaphere
{
public:
	Semaphere(int limit = 0) : resLimit_(limit){}
	~Semaphere() = default;
	// ����һ���ź�����Դ
	void wait()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		// ������Դ������²ſ�������
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// ����һ���ź�����Դ
	void post()
	{
		std::unique_lock<std::mutex>lock(mtx_);
		resLimit_++;
		cond_.notify_all();// ���������߳��ź���+1
	}



private:
	int resLimit_;// ��Դ����
	std::mutex mtx_;
	std::condition_variable cond_;
};


// ʵ�� �����ύ���̳߳�task����ִ����ɺ�ķ���ֵ����
// �������ڹ����߳���ִ�еģ��û�����get����������÷���ֵ�������һ���̣߳���Ҫ��ִ���꣬�û����ܻ�ã���Ƶ��߳�ͨ�ţ�--���ź���
class Task;
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isvalid = true);
	~Result() = default; // д������default�ؼ��������ڱ������Ż�	
	
	// ��λ�ȡ����ִ���꣨task-��run���ķ���ֵany������run��������ֵ��Ϊ��������any����Reslut�еĶ�������Ա���������
	void setVal(Any any);


	// �û�����get����������ȡtask�ķ���ֵ��
	Any get();


private:
	Any any_;//�洢����ķ���ֵ
	Semaphere sem_;
	std::shared_ptr<Task>task_;//ָ����ڻ�ȡ����ֵ��������󡣻��submitTask�еĲ�������
	// ��Ϊ�� Result��task�� ��֤ Result�������ж��� ���Կ���һ��task���󣬻�ȡtask���� ִ��runc�ķ���ֵ��
	std::atomic_bool isValid_;//�ж�����task�Ƿ���Ч��
};




class Thread
{
public:
	using ThreadFunc = std::function<void(int)>; //�����̺߳����������͡�
	
	Thread(ThreadFunc func);
	~Thread();
	int getId()const;

	void start();
private:
	ThreadFunc func_; // ��������
	static int generateId_;
	int threadId_;// �����߳�id��
};


//�����ࣺ������������û��ṩ����˴�����ߵĽǶȳ�������Ƴ�����������Ӧ���ܽ����û����ݹ������������ͣ�����Ĳ����ɲ�ͬ�����;���
//��˿������ ����Ϊ������� �Ϳ��Խ����û����������͡�

// ����ֱ����Task����ֱ�Ӱ���Result�࣬��Ϊһ��Task��Ķ���û�ˣ���Ӧ��Resut��Ķ���Ҳ�ᱻ���գ�����ָ�뼴�ɡ�
class Task
{
public:
	void exec();
	void setResult(Result* res);
	Task();
	~Task() = default;
	virtual Any run() = 0;// �û������Զ��������������ͣ���task�̳У���дrun���� ������ʵ���Զ����������
private:
	Result*  result_;// Result �������������ҲӦ�ô��ڼ̳�Task�������û���ƵĶ���
};




// �߳�֧�ֵ�ģʽ
enum  PoolMode
{
	MODE_FIXED,// �̶��߳�����
	MODE_CACHED,// �߳������ɶ�̬����
};


//�̳߳���
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	
	// �����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);
	/*void setInitThreadSize(int size);*/

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	void setThreadSizeThreshHold(int threshhold);//�����̳߳�cacheģʽ���߳���ֵ����Ϊ�û���cpu����������ˣ�ͨ��һ���������̵߳����ֵ�����û��趨��

	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);
	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());


	// ��Ϊ��ϣ�������͸�ֵ,�̳߳ض���Ķ���̫���ˣ���ֵ�����Ȳ���ȫ����Ҳ��
	ThreadPool(const ThreadPool& s) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �����̺߳��� ��ԭ��Ӧ�÷����߳����У������̺߳���Ҫ���ʵ����Զ���threadPool��private�����У��ɴ����ThreadPool���ж����̵߳���������
	void threadFunc(int threadid);// �߳������߳�Ӧ���������顣

	// ThreadPool�е�set����ֻ�����߳����е�ʱ��������У����дһ���ж��߳��Ƿ��������еĺ�������Ϊ���Ǹ������õĽӿڣ������Լ��ĳ�Ա������ʹ��
	// Ȩ������Ϊprivate
	//����̳߳ص�״̬
	bool checkRunningState()const;

private:
	/*std::vector<Thread*>threads_;*///�߳��б�   // ����̵߳�������ֻ����������ÿ����̰߳�ȫ���⡣
	// ���ǻ��������⣬��Ϊ�����ڴ����̶߳����ʱ�� ������new �����������Ҫ�ֶ��ͷ��ڴ棬Ϊ�˱����ֶ��ͷ��ڴ�ķ���---����������ָ��
	
	/*std::vector<std::unique_ptr<Thread>>threads_;*/
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;//int --> �߳�id ����ʶ�߳������е�ÿһ��ָ���̶߳����ָ�롣
	
	size_t initThreadSize_;// ��ʼ���߳�����
	//size_t MaxThreadSize_;// 

	// �߳��������޵���ֵ��cachedģʽ��
	int threadSizeThreshHold_;
	// ��¼��ǰ�̳߳������̵߳������� ����Ϊ����ʱ�ı�--����ֹ���̵߳�����--������atomic_int����
	std::atomic_int curThreadSize_;
	// ��¼�����̵߳�����
	std::atomic_int idleThreadSize_;
	

	std::queue<std::shared_ptr<Task>>taskQue_;// �������	
			// �����û���ָ�룬��Ϊֻ�л����ָ������ò��ܽ����������
			
	// �̻߳�������
	// �߳�����һ�������û�����һ������--�����߳�����--���̰߳�ȫ����Ϊֻ��++��--����Ҫ����---����atomic���ɡ�																		// ����������ָ�룺��Ϊ�û����ܴ����Ķ�������ʱ���������ʱ�������٣�Task*ָ��һ�鱻�ͷŵ��ڴ�����ָ���쳣--->�������Ӧ��ʹ��ֻ��ָ��
	std::atomic_int taskSize_;//���������
	int taskQueMaxThreshHold_;//�����������������ֵ��

	// �߳�ͨ�ţ������� + ����������
	std::mutex taskQueMtx_;//��֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в���
	std::condition_variable notEmpty_;//��ʾ������в���
	std::condition_variable exitCond_;// �ȴ����߳���Դȫ������

	PoolMode PoolMode_;//��ǰ�̳߳ص�ģʽ



	// ��ʾ��ǰ�̵߳�����״̬����Ϊ�ڶ���߳��� �����ܻ��õ������--��Ƴ�ԭ�����͡�
	 std::atomic_bool isPoolRunning_;
	 
};






#endif

