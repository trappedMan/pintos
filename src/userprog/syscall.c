#include "userprog/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "lib/stdbool.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "pagedir.h"
#include "lib/stdbool.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include <console.h>
#include "devices/input.h"
#include "process.h"
#include "threads/synch.h"
#include <string.h>
#include "filesys/off_t.h"
typedef int pid_t;
static void syscall_handler (struct intr_frame *);
void check_addr(uint32_t *esp,int arg_size);

struct file{
	struct inode *inode;
	off_t pos;
	bool deny_write;
};

void halt(void);
void exit(int status);
pid_t exec(const char* arg);
int wait(pid_t pid);
int write(int fd,const void* buf,unsigned int size);
int read(int fd,void* buf,unsigned int size);
int fibonacci(int n);
int max_of_four_int(int a,int b,int c,int d);
bool create(const char* file,unsigned int initial_size);
bool remove(const char* file);
int open(const char* file);
void close(int fd);
int filesize(int fd);
void seek(int fd,unsigned int position);
unsigned int tell(int fd);
bool chdir(char *);
bool mkdir(char *);
bool readdir(int, char *);
bool isdir(int x);
int inumber(int x);
struct inode{
	struct list_elem elem;
	block_sector_t sector;

	int open_cnt;
	bool removed;

	int deny_write_cnt;
	struct lock lock_inode;
};
void
syscall_init (void) 
{
  lock_init(&rw_filelock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void check_addr(uint32_t* esp,int arg_size){
	for(int i=0;i<=arg_size;i++){
		if(!(esp+i)){
			//printf("NULL PTR\n");
			exit(-1);
		}

		else if(!is_user_vaddr((void *)esp[i])){
			//printf("Accessing Kernel Address\n");
			exit(-1);
		}
		//else if(!pagedir_get_page(thread_current()->pagedir,esp+i)){
			//printf("Paging Error\n");
		//	exit(-1);
		//}
	}
}

static void
syscall_handler (struct intr_frame *f) 
{
	uint32_t* esp32_ptr=(uint32_t*)(f->esp);
	uint32_t sys_num=*(uint32_t*)(f->esp);
	switch(sys_num){
		case SYS_HALT:
			check_addr(esp32_ptr,0);
			halt();	
			break;
		case SYS_EXIT:
			check_addr(esp32_ptr,1);
			exit((int)esp32_ptr[1]);
			break;
		case SYS_EXEC:
			check_addr(esp32_ptr,1);
			f->eax=exec((char *)esp32_ptr[1]);
			break;
		case SYS_WAIT:
			check_addr(esp32_ptr,1);
			f->eax=wait((pid_t)esp32_ptr[1]);
			break;
		case SYS_READ:
			check_addr(esp32_ptr,3);
			f->eax=read((int)esp32_ptr[1],esp32_ptr[2],(unsigned int)esp32_ptr[3]);
			break;
		case SYS_WRITE:
			check_addr(esp32_ptr,3);
			f->eax=write((int)esp32_ptr[1],esp32_ptr[2],(unsigned int)esp32_ptr[3]);
			break;
		case SYS_FIBO:
			check_addr(esp32_ptr,1);
			f->eax=fibonacci((int)esp32_ptr[1]);
			break;
		case SYS_MAXFOUR:
			check_addr(esp32_ptr,4);
			f->eax=max_of_four_int((int)esp32_ptr[1],(int)esp32_ptr[2],(int)esp32_ptr[3],(int)esp32_ptr[4]);
			break;
		case SYS_CREATE:
			check_addr(esp32_ptr,2);
			f->eax=create((char *)esp32_ptr[1],(unsigned int)esp32_ptr[2]);
			break;
		case SYS_REMOVE:
			check_addr(esp32_ptr,1);
			f->eax=remove((char *)esp32_ptr[1]);
			break;
		case SYS_OPEN:
			check_addr(esp32_ptr,1);
			f->eax=open((char *)esp32_ptr[1]);
			break;
		case SYS_CLOSE:
			check_addr(esp32_ptr,1);
			close((int)esp32_ptr[1]);
			break;
		case SYS_FILESIZE:
			check_addr(esp32_ptr,1);
			f->eax=filesize((int)esp32_ptr[1]);
			break;
		case SYS_SEEK:
			check_addr(esp32_ptr,2);
			seek((int)esp32_ptr[1],(unsigned int)esp32_ptr[2]);
			break;
		case SYS_TELL:
			check_addr(esp32_ptr,1);
			f->eax=tell((int)esp32_ptr[1]);
			break;
		case SYS_CHDIR:
			check_addr(esp32_ptr,1);
			f->eax=chdir((char *)esp32_ptr[1]);
			break;
		case SYS_MKDIR:
			check_addr(esp32_ptr,1);
			f->eax=mkdir((char *)esp32_ptr[1]);
			break;
		case SYS_READDIR:
			check_addr(esp32_ptr,2);
			f->eax=readdir((int)esp32_ptr[1],(char *)esp32_ptr[2]);
			break;
		case SYS_ISDIR:
			check_addr(esp32_ptr,1);
			f->eax=isdir((int)esp32_ptr[1]);
			break;
		case SYS_INUMBER:
			check_addr(esp32_ptr,1);
			f->eax=inumber((int)esp32_ptr[1]);
			break;


	}
}
//implementations of syscall functions
void halt(){
	shutdown_power_off();
}

void exit(int status){
	thread_current()->exit_status=status;
	const char* name=thread_current()->name;
	char proc_name[200];
	int i;
	for(i=0;i<(int)strlen(name);i++){
		if(name[i]==' ')
			break;
	}
	strlcpy(proc_name,name,i+1);
	printf("%s: exit(%d)\n",proc_name,status);
	for(int i=3;i<128;i++){
		if(thread_current()->desc[i]){
			struct inode* inode=file_get_inode(thread_current()->desc[i]);
			if(inode&&inode->deny_write_cnt>0){
				if(!is_direc(inode))
					file_close(thread_current()->desc[i]);
				else
					dir_close(thread_current()->desc[i]);
			}	
			thread_current()->desc[i]=NULL;
		}
	}
	if(thread_current()->direc) dir_close(thread_current()->direc);
	
	/*struct thread *tmp,*t=thread_current();
	for(struct list_elem* l=list_begin(&(t->children_list));l!=list_end(&(t->children_list));l=list_next(l)){
		tmp=list_entry(l,struct thread,child_node);
		process_wait(tmp->tid);
	}*/
	thread_exit();
}

pid_t exec(const char* arg){
	char str[200];
	int i=0;
	while(arg[i]!=' '){
		i++;
	}
	strlcpy(str,arg,i+1);
	if(filesys_open(str)==NULL)
		return -1;
	return (pid_t)process_execute(arg);	
}

int wait(pid_t pid){
	return process_wait(pid);
}

int write(int fd,const void* buf,unsigned int size){
	int result;
	if(fd==1){
		lock_acquire(&rw_filelock);
		putbuf(buf,size);
		lock_release(&rw_filelock);
		return size;
	}
	else if(thread_current()->desc[fd]==NULL){
		return -1;
	}
	if(isdir(fd)) return -1;//dir-open pass, dir-open-persistence	
	lock_acquire(&rw_filelock);
	struct file *f=thread_current()->desc[fd];
	result=(int)file_write(thread_current()->desc[fd],buf,size);
	lock_release(&rw_filelock);
	return result;
}

int read(int fd,void *buf,unsigned int size){
	int result;
	char key;
	if(fd==0){
		int i;
		lock_acquire(&rw_filelock);
		for(i=0;i<size;i++){
			key=(char)input_getc();
			if(key){
				*(char *)(buf+i)=key;
			}
			else{
				*(char *)(buf+i)=key;
				break;
			}
		}
		lock_release(&rw_filelock);	
		return i;
	}
	else if(thread_current()->desc[fd]==NULL){
		return -1;
	}
	lock_acquire(&rw_filelock);
	result=file_read(thread_current()->desc[fd],buf,size);
	lock_release(&rw_filelock);
	return result;
}

int fibonacci(int n){
	if(n==1||n==2)
		return 1;
	int f=1,s=1,nxt=1;
	while(n-->2){
		nxt=f+s;
		f=s;
		s=nxt;
	}
	return nxt;
}

int max_of_four_int(int a,int b,int c,int d){
	int tmp=a;
	if(tmp<b)
		tmp=b;
	if(tmp<c)
		tmp=c;
	if(tmp<d)
		tmp=d;
	return tmp;
}

bool create(const char *file,unsigned int initial_size){
	if(!file)
		//return false;
		exit(-1);
	//lock_acquire(&rw_filelock);
	bool result=filesys_create(file,initial_size);
	//lock_release(&rw_filelock);
	return result;
}

bool remove(const char *file){
	if(!file)
		//return false;
		exit(-1);
	//lock_acquire(&rw_filelock);
	bool result=filesys_remove(file);
	//lock_release(&rw_filelock);
	return result;	
}

int open(const char *file){
	int i;
	if(!file)
		exit(-1);
	lock_acquire(&rw_filelock);
	struct file *f=filesys_open(file);
	lock_release(&rw_filelock);
	if(!f)
		return -1;
	for(i=3;i<128;i++){
		if(thread_current()->desc[i]==NULL)
			break;
	}
	if(i==128)
		return -1;
	if(strcmp(thread_name(),file)==0){
		file_deny_write(f);
	}
	thread_current()->desc[i]=f;
	return i;	
}

void close(int fd){
	if(!(thread_current()->desc[fd]))
		exit(-1);
	//lock_acquire(&rw_filelock);
	file_close(thread_current()->desc[fd]);
	thread_current()->desc[fd]=NULL;
	//lock_release(&rw_filelock);
}

int filesize(int fd){
	int32_t len;
	if(!(thread_current()->desc[fd]))
		exit(-1);
	lock_acquire(&rw_filelock);
	len=file_length(thread_current()->desc[fd]);
	lock_release(&rw_filelock);
	return len;	
}

void seek(int fd,unsigned int position){
	if(!(thread_current()->desc[fd]))
		exit(-1);
	lock_acquire(&rw_filelock);
	file_seek(thread_current()->desc[fd],position);
	lock_release(&rw_filelock);
}


unsigned int tell(int fd){
	int32_t result;
	if(!(thread_current()->desc[fd]))
		exit(-1);
	lock_acquire(&rw_filelock);
	result=file_tell(thread_current()->desc[fd]);
	lock_release(&rw_filelock);
	return (unsigned int)result;
}

bool chdir(char *path){
	return filesys_change_dir(path);
}

bool mkdir(char *name){
	return filesys_create_dir(name);
}

bool readdir(int x, char *name){
	bool success=true;

	if(thread_current()->desc[x]==NULL)
		exit(-1);

	struct file *f=thread_current()->desc[x];
	struct inode* i=file_get_inode(f);
	if(!i||!is_direc(i)) return false;

	struct dir *dir=dir_open(i);
	dir->pos=file_tell(f);
	success=dir_readdir(dir,name);
	file_seek(f,dir->pos);
	return success;
}

bool isdir(int x){
	if(thread_current()->desc[x]==NULL)
		exit(-1);
	return is_direc(file_get_inode(thread_current()->desc[x]));
}

int inumber(int x){
	if(thread_current()->desc[x]==NULL)
		exit(-1);
	return inode_get_inumber(file_get_inode(thread_current()->desc[x]));
}
