#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <thread>
#include <vector>
#include <mutex>
#include <future>
#include <queue>

class ThreadPool final
{
private:
	using TaskFunction = void(const unsigned int id);

	std::queue<std::function<TaskFunction> *> _tasks;
	std::vector<std::unique_ptr<std::thread>> _threads;
	std::vector<std::shared_ptr<std::atomic<bool>>> _threadsStopFlags;
	std::atomic<int> _idleThreadsCount { 0 };
	std::atomic<bool> _doneFlag { false };
	std::atomic<bool> _stopFlag { false };
	std::mutex _threadsMutex;
	std::mutex _tasksMutex;
	std::condition_variable _signal;

public:
	ThreadPool()
	{
		Init(std::thread::hardware_concurrency());
	}

	ThreadPool(const unsigned int threadsCount)
	{
		Init(threadsCount);
	}

	~ThreadPool()
	{
		Stop(true);
	}

	unsigned int ThreadsCount() const
	{
		return _threads.size();
	}

	int IdleThreadsCount() const
	{
		return _idleThreadsCount;
	}

	void Stop(const bool waitForThreads)
	{
		if(waitForThreads)
		{
			if (_doneFlag || _stopFlag)
				return;
			_doneFlag = true;
		}
		else
		{
			if (_stopFlag)
				return;

			_stopFlag = true;
			for(unsigned int i = 0, tc = ThreadsCount(); i < tc; i++)
			{
				*_threadsStopFlags[i] = true;
			}

			ClearTasks();
		}

		{
			std::unique_lock<std::mutex> lock(_threadsMutex);
			_signal.notify_all(); // Stop all idle threads
		}

		for(unsigned int i = 0; i < ThreadsCount(); i++)
		{
			if(_threads[i]->joinable())
			{
				_threads[i]->join();
			}
		}

		ClearTasks();
		_threads.clear();
		_threadsStopFlags.clear();
	}

	template<typename T>
	auto Enqueue(T &&task) -> std::future<decltype(task(0))>
	{
		auto packagedTask = std::make_shared<std::packaged_task<decltype(task(0))(unsigned int)>>(std::forward<T>(task));
		auto pTask = new std::function<TaskFunction>([packagedTask](unsigned int id) { (*packagedTask)(id); });
		PushTask(pTask);
		std::unique_lock<std::mutex> lock(_threadsMutex);
		_signal.notify_one();
		return packagedTask->get_future();
	}

	template<typename T, typename... PTs>
	auto Enqueue(T &&task, PTs &&... paramTypes) -> std::future<decltype(task(0, paramTypes...))>
	{
		auto packagedTask = std::make_shared<std::packaged_task<decltype(task(0, paramTypes...))(unsigned int)>>(
			std::bind(std::forward<T>(task), std::placeholders::_1, std::forward<PTs>(paramTypes)...)
			);
		auto pTask = new std::function<TaskFunction>([packagedTask](unsigned int id) { (*packagedTask)(id); });
		PushTask(pTask);
		std::unique_lock<std::mutex> lock(_threadsMutex);
		_signal.notify_one();
		return packagedTask->get_future();
	}

	// Deleted member functions
	ThreadPool(const ThreadPool &) = delete;
	ThreadPool(ThreadPool &&) = delete;
	ThreadPool &operator=(const ThreadPool &) = delete;
	ThreadPool &operator=(ThreadPool &&) = delete;

private:
	void Init(const unsigned int threadsCount)
	{
		if (_stopFlag || _doneFlag)
			return;

		const auto currentThreadsCount = ThreadsCount();
		if (currentThreadsCount == threadsCount)
			return;

		if(currentThreadsCount < threadsCount)
		{
			_threads.resize(threadsCount);
			_threadsStopFlags.resize(threadsCount);

			for(unsigned int i = currentThreadsCount; i < threadsCount; i++)
			{
				_threadsStopFlags[i] = std::make_shared<std::atomic<bool>>(false);
				InitThead(i);
			}
		}
		else
		{
			for(unsigned int i = currentThreadsCount - 1; i >= threadsCount; i--)
			{
				*_threadsStopFlags[i] = true;
				_threads[i]->detach();
			}

			{
				// Stop idle detached threads
				std::unique_lock<std::mutex> lock(_threadsMutex);
				_signal.notify_all();
			}

			_threads.resize(threadsCount);
			_threadsStopFlags.resize(threadsCount);
		}
	}

	void InitThead(const unsigned int index)
	{
		std::shared_ptr<std::atomic<bool>> stopFlag(_threadsStopFlags[index]);

		auto threadFunction = [this, index, stopFlag]()
		{
			std::atomic<bool> &threadStopFlag = *stopFlag;
			std::function<TaskFunction> *taskFunction;

			bool isTaskAvailable = this->PopTask(taskFunction);
			while (true)
			{
				while (isTaskAvailable)
				{
					std::unique_ptr<std::function<TaskFunction>> pTaskFunc(taskFunction);	// Delete the task when it is done
					(*taskFunction)(index);

					if (threadStopFlag)
						return;

					isTaskAvailable = this->PopTask(taskFunction);
				}

				// No more tasks to run, wait for new tasks
				std::unique_lock<std::mutex> lock(this->_threadsMutex);
				++this->_idleThreadsCount;
				this->_signal.wait(lock,
					[this, &taskFunction, &isTaskAvailable, &threadStopFlag] {
					isTaskAvailable = this->PopTask(taskFunction);
					return isTaskAvailable || this->_doneFlag || threadStopFlag;
				});
				--this->_idleThreadsCount;

				if (!isTaskAvailable)
					return;
			}
		}; // End of threadFunction

		_threads[index].reset(new std::thread(threadFunction));
	}

	void PushTask(std::function<TaskFunction> *const &task)
	{
		std::unique_lock<std::mutex> lock(_tasksMutex);
		_tasks.push(task);
	}

	bool PopTask(std::function<TaskFunction> * &task)
	{
		std::unique_lock<std::mutex> lock(_tasksMutex);
		if (_tasks.empty())
			return false;

		task = _tasks.front();
		_tasks.pop();
		return true;
	}

	void ClearTasks()
	{
		std::function<TaskFunction> *pTask;
		while(PopTask(pTask))
		{
			delete pTask;
		}
	}
};

#endif