#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>

int main(int argc,char *argv[]){
	if(argc!=5){
		printf("4 arguments needed\n");
		return -1;
	}
	int fibo,maxval,args[4];
	for(int i=0;i<=3;i++){
		args[i]=atoi(argv[i+1]);
	}
	fibo=fibonacci(args[0]);
	maxval=max_of_four_int(args[0],args[1],args[2],args[3]);
	printf("%d %d\n",fibo,maxval);
	return EXIT_SUCCESS;
}

