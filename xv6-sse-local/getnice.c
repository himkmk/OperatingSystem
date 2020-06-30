#include "types.h"
#include "stat.h"
#include "user.h"


int
main(int argc, char* argv[])
{
	
	int pid;
	
	if (argc<2){ //print error
		printf(2, "Need More arguments. getnice(int pid)\n");
		exit();
	}
	
	pid = atoi(argv[1]);
	
	//error for out-of-boundary pid,value
	if (pid<0){
		printf(2,"pid cannot be negative\n");
		exit();
	}

	//if works properly
	
	printf(2,"Nice Value of pid=%d is %d\n",pid,getnice(pid));
	exit();
}

