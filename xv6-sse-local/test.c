#include "types.h"
#include "stat.h"
#include "user.h"


int
main(int argc, char *argv[]){  

	int n,f_id;
	float f=479.2344,d=1.0;

	printf(1,"\n\n this is test.c for dummy functions \n\n");
	
	if (argc<2) {n=1;}
	else {n = atoi(argv[1]);}
	if ( n<0 || n>5) n=2;

	for (int i=0; i<n; i++){
		f_id = fork();
		if (f_id < 0) {printf(1,"%d failed to fork. \n", getpid());}
		else if (f_id ==0) {
			printf(1,"Child %d created. \n", getpid());
			for(float i=0; i<9797939.912; i+=d){
				//dummy function
				f=f/23.412;
				d=(float)((int)d/f)+1.0;			
			}
			break;
		}
		else if (f_id>0)   {
			printf(1,"Parent %d creating child %d\n",getpid(),f_id);
			wait();
		}
	}
	
  
 	exit();
}
