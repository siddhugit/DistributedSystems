#include "common.h"


//#define HOME_DIR "/home/siddharth/cs425/"
#define HOME_DIR "/home/ss31/mj/"

std::string getLocalTempDir()
{
	return (std::string(HOME_DIR) + "dir/temp/");
}
std::string getKeyFileName()
{
	return (std::string(HOME_DIR) + "dir/keys/keys");
}
std::string getHomeDir()
{
	return HOME_DIR;
}
