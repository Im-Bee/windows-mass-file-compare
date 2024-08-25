#include <iostream>
#include <future>
#include <exception>
#include <fstream>
#include <filesystem>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif // !WIN32_LEAN_AND_MEAN

using namespace std;

#define MEM_KB(x) static_cast<UINT64>(x * 1024)

/*
* Compare two buffers 
*/
BOOL CompareBuffers(
	char* a,
	char* b,
	const UINT64& maxSize)
{
	for (UINT64 i = 0; i < maxSize; ++i)
	{
		if (a[i] != b[i])
			return false;
	}

	// No differences
	return true;
}

/*
* Compare file A with B
* @param fileA - Path to file A
* @param fileB - Path to file B
* @return True if the files were identical
*/
bool CompareFiles(const string& fileA, const string& fileB)
{
	bool result = true;
	const UINT64 fileSizeA = filesystem::file_size(fileA);
	const UINT64 fileSizeB = filesystem::file_size(fileB);
	if (fileSizeA > fileSizeB)
	{
		result = false;
		return result;
	}

	ifstream ifsA(fileA, ios_base::binary);
	ifstream ifsB(fileB, ios_base::binary);
	constexpr UINT64 uCachesSize = MEM_KB(32);
	char* pCacheAFront = nullptr;
	char* pCacheABack = nullptr;
	char* pCacheBFront = nullptr;
	char* pCacheBBack = nullptr;

	if (!ifsA.is_open())
		throw invalid_argument("Couldn't open the file");
	if (!ifsB.is_open())
		throw invalid_argument("Couldn't open the file");

	pCacheAFront = (char*)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, uCachesSize);
	pCacheABack  = (char*)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, uCachesSize);
	pCacheBFront = (char*)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, uCachesSize);
	pCacheBBack  = (char*)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, uCachesSize);

	ifsA.read(pCacheABack, uCachesSize);
	ifsB.read(pCacheBBack, uCachesSize);

	char* pTmp;
	while (!ifsA.eof() &&
		!ifsB.eof())
	{
		auto aCmp = async(launch::async, CompareBuffers, pCacheABack, pCacheBBack, uCachesSize);
		auto aReadA = async(launch::async, &ifstream::read, &ifsA, pCacheAFront, uCachesSize);
		auto aReadB = async(launch::async, &ifstream::read, &ifsB, pCacheBFront, uCachesSize);

		if (!aCmp.get())
		{
			result = false;
			break;
		}
		aReadA.wait();
		aReadB.wait();

		// Swap A
		pTmp = pCacheABack;
		pCacheABack = pCacheAFront;
		pCacheAFront = pTmp;

		// Swap B
		pTmp = pCacheBBack;
		pCacheBBack = pCacheBFront;
		pCacheBFront = pTmp;
	}

	if (!CompareBuffers(pCacheABack, pCacheBBack, uCachesSize))
		result = false;

	ifsA.close();
	ifsB.close();

	HeapFree(GetProcessHeap(), 0, pCacheAFront);
	HeapFree(GetProcessHeap(), 0, pCacheABack);
	HeapFree(GetProcessHeap(), 0, pCacheBFront);
	HeapFree(GetProcessHeap(), 0, pCacheBBack);

	return result;
}

void CompareFilesThreaded(string fileA, string fileB, string& out)
{
	try
	{
		if (!CompareFiles(fileA, fileB))
			out = fileA;

		return;
	}
	catch (exception e)
	{
		out = e.what();
		out += ": ";
		out += fileA;

		return;
	}

	out = string();
}

vector<string> CompareDirectoriesThreaded(const string& dirA, const string& dirB)
{
	vector<string> reservedStrings = {};
	vector<string> result = {};
	vector<thread> threads = {};

	// Reserve space for the results
    for (auto& file : filesystem::directory_iterator(dirA))
	{
		reservedStrings.push_back(string());
	}

	UINT64 i = 0;
	for (auto& file : filesystem::directory_iterator(dirA))
	{
		if (file.is_directory())
			continue;

		threads.push_back(thread(
			CompareFilesThreaded, 
			string(dirA + (dirA.back() == '\\' ? "" : "\\") + file.path().filename().string()),
			string(dirB + (dirA.back() == '\\' ? "" : "\\") + file.path().filename().string()),
			ref(reservedStrings[i++])));
	}

	for (auto& t : threads)
		t.join();

	for (auto& s : reservedStrings)
	{
		if (!s.empty())
			result.push_back(s);
	}

	return result;
}

vector<string> CompareDirectories(const string& dirA, const string& dirB)
{
	vector<string> result = {};

	string tmpFullPathA = string(),
		tmpFullPathB = string();
	for (auto& file : filesystem::directory_iterator(dirA))
	{
		tmpFullPathA = dirA + (dirA.back() == '\\' ? "" : "\\") + file.path().filename().string();
		tmpFullPathB = dirB + (dirA.back() == '\\' ? "" : "\\") + file.path().filename().string();

		try
		{
			if (!CompareFiles(tmpFullPathA, tmpFullPathB))
				result.push_back(tmpFullPathA);
		}
		catch (exception e)
		{
			string out = string();
			out += +e.what();
			out += ": ";
			out += tmpFullPathA;

			result.push_back(out);
		}
	}

	return result;
}

int main(int argc, char* argv[])
{
	if (argc < 3)
	{
		cout << "Bad args" << endl;
		return 0;
	}

	auto result = CompareDirectoriesThreaded(argv[1], argv[2]);
	for (auto& fileName : result)
	{
		cout << fileName << endl;
	}

	return 0;
}
