// ThreadPool.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "ThreadPool.h"

using namespace std;

mutex _outputMutex;

int GetRandomNumber(int min, int max)
{
	random_device dev;
	mt19937 rng(dev());
	const uniform_int_distribution<mt19937::result_type> dist(min, max);
	return dist(rng);
}

void Task01(unsigned int id)
{
	lock_guard<mutex> lock(_outputMutex);
	cout << "Task01 - Thread ID: " << id << endl;
}

void Task02(unsigned int id, int num1)
{
	lock_guard<mutex> lock(_outputMutex);
	cout << "Task02 - Thread ID: " << id << " , Num1: " << num1 << endl;
}

int Task03(unsigned int id, int milliseconds, int num1, int num2)
{
	const auto start = chrono::high_resolution_clock::now();
	this_thread::sleep_for(chrono::milliseconds(milliseconds));
	const auto end = chrono::high_resolution_clock::now();
	
	chrono::duration<double, milli> elapsed = end - start;
	{
		lock_guard<mutex> lock(_outputMutex);
		cout << "Task03 - Thread ID: " << id << " , Waited: " << elapsed.count() << " ms." << endl;
	}
	return num1 * num2;
}

int main()
{
	ThreadPool threadPool(2);
	cout << "Idle threads: " << threadPool.IdleThreadsCount() << endl;

	future<void> f01 = threadPool.Enqueue(ref(Task01));
	threadPool.Enqueue(Task01);
	threadPool.Enqueue(Task02, 100);
	auto f02 = threadPool.Enqueue(Task03, GetRandomNumber(2000, 5000), 10, 20);
	auto f03 = threadPool.Enqueue(Task03, GetRandomNumber(2000, 5000), 20, 30);

	std::string name = "Task04";
	threadPool.Enqueue([name](unsigned int id)
	{
		this_thread::sleep_for(chrono::milliseconds(GetRandomNumber(1000, 2000)));
		lock_guard<mutex> lock(_outputMutex);
		cout << name << " - Thread ID: " << id << ", Done." << endl;
	});

	auto f04 = threadPool.Enqueue([](unsigned int id)
	{
		stringstream ss;
		ss << "Task05 - Thead ID: " << id << " - Exception";
		throw exception(ss.str().c_str());
	});

	{
		const auto f02Result = f02.get();
		const auto f03Result = f03.get();
		lock_guard<mutex> lock(_outputMutex);
		cout << "f02 = " << f02Result << endl;
		cout << "f03 = " << f03Result << endl;
	}

	try
	{
		f04.get();
	}
	catch(exception & e)
	{
		cout << "Exception: " << e.what() << endl;
	}

	for(int i = 0; i < 10; i++)
	{
		threadPool.Enqueue(Task03, GetRandomNumber(2000, 5000), i, 10);
	}

	return 0;
}
