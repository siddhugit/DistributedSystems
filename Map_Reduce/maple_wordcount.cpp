#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <sstream>
#include <algorithm>

bool isNonAlphaNumeric(char c)
{
	bool result  = ((c >= '0' && c <='9') || (c >= 'a' && c <='z'));
	return !(result);
}

void normalizeWord(std::string& word)
{
	std::transform(word.begin(), word.end(), word.begin(), ::tolower);
	word.erase(std::remove_if(word.begin(), word.end(), isNonAlphaNumeric), word.end());
}

int main(int argc,char** argv)
{
	for(int i = 1;i < argc;++i)
	{
		std::string fileName = argv[i];
		std::ifstream infile( fileName.c_str());
		std::string line;
		while (std::getline(infile, line))
		{
			std::istringstream iss(line);
			std::string word;
			while(iss >> word)
			{
				normalizeWord(word);
				std::cout<<word<<","<<1<<"\n";
			}
		}
	}
}
