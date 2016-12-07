// classes example
#include <iostream>
#include "string.h"
using namespace std;

class Filesystem {
    
  public:
    Filesystem(std::string);
    bool isMaster() {return master;}
    bool isMasterBackup() {return masterBackup;}

    
  private:
    bool master;
    bool masterBackup;
};

