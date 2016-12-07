#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>

int handleKey(const std::string& key,const std::string& inputDir,const std::string& prefix)
{
	std::string fileName = inputDir + prefix + "_" + key;
	std::ifstream inFile(fileName.c_str());
	std::string line;
	int count = 0;
	while(std::getline(inFile,line))
	{
		std::string::size_type pos = line.find_first_of(',');
		std::string key = line.substr(0, pos);
		std::string val = line.substr(pos + 1, line.size());
		count += atoi(val.c_str());
	}
	return count;
}

int main(int argc,char** argv)
{
	std::string inputDir = argv[1];
	std::string prefix = argv[2];
	for(int i = 3;i < argc; ++i)
	{
		std::string key = argv[i];
		int result = handleKey(key,inputDir,prefix);
		std::cout<<key<<","<<result<<"\n";
	}
}
