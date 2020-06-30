#include "types.h"
#include "stat.h"
#include "user.h"


int
main(int argc, char* argv[])
{
	
	int pid,value;
	
	if (argc<2){ //print error
		printf(2, "Need More arguments. setnice(int pid, int value)\n");
		exit();
	}
	
	pid = atoi(argv[1]);
	value = atoi(argv[2]);
	
	//error for out-of-boundary pid,value
	if (value<0 || value >39){
		printf(2,"nice value out of range ( need 0~39)");
		exit();
	}

	//if works properly
	setnice(pid, value);
	
	exit();
}

