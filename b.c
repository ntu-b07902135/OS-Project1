#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<stdio.h>
#include<stdlib.h>
#include<errno.h>
#include<time.h>
#include<fcntl.h>
#include<sched.h>
#include<limits.h>
#include<signal.h>

typedef struct{
	char N[256];
	int R;
	int T;
	int TT;
	int pid;
	struct timespec st;
	struct timespec et;
} process;

int n;
process sched[256];
int down_pipe[256][2];
int up_pipe[256][2];
int t_now, t_last;

void run_one_unit(){
	volatile unsigned long i;
	for(i = 0; i < 1000000UL; i++);
}

void child_process(int i){
	char buf;
	struct timespec start, end;
	for(int j = 0; j < sched[i].T; j++){
		read(down_pipe[i][0], &buf, 1);
		if(!j){
			clock_gettime(0, &start);
		}
		run_one_unit();
	}
	clock_gettime(0, &end);
	write(up_pipe[i][1], &start, sizeof(start));
	write(up_pipe[i][1], &end, sizeof(end));
}

int fork_process(int i){
	pipe(up_pipe[i]);
	pipe(down_pipe[i]);
	int c_pid = fork();
	if(!c_pid){ //child process
		close(up_pipe[i][0]);
		close(down_pipe[i][1]);
		//fprintf(stderr, "child %d forked\n", i);
		child_process(i);
		exit(0);
	}else{
		close(up_pipe[i][1]);
		close(down_pipe[i][0]);
	}
	return c_pid;
}

int next_process(int running_idx, char* policy){
	if(running_idx != -1){
		if(policy[0] == 'S' || policy[0] == 'F'){
			return running_idx;
		}
	}
	int result = -1;
	switch(policy[0]){
	case 'F':
		for(int i = 0; i < n; i++){
			if(result == -1 || sched[i].R < sched[result].R){
				if(sched[i].pid > 0 && sched[i].T > 0){
					result = i;
				}
			}	
		}
		break;
	case 'R':
		if(running_idx == -1){
			for(int i = 0; i < n; i++){
				if(sched[i].pid > 0 && sched[i].T > 0){
					result = i;
					break;
				}
			}
		}else if((t_now - t_last) % 500 == 0){
			result = (running_idx + 1) % n;
			while(sched[result].pid < 1 || sched[result].T < 1){
				result = (result + 1) % n;
			}
		}else{
			result = running_idx;
		}
		break;
	default:
		for(int i = 0; i < n; i++){
			if(result == -1 || sched[i].T < sched[result].T){
				if(sched[i].pid > 0 && sched[i].T > 0){
					result = i;
				}
			}	
		}
	}
	return result;
}

struct sched_param param;

int main(){
	param.sched_priority = 0;
	sched_setscheduler(getpid(), SCHED_OTHER, &param);
	int nul = open("/dev/null", O_WRONLY);
	char policy[256] = {};
	scanf("%s%d", policy, &n);
	for(int i = 0; i < n; i++){
		scanf("%s%d%d", sched[i].N, &(sched[i].R), &(sched[i].T));
		sched[i].TT = sched[i].T;
	}
	sched[n].T = INT_MAX;
	sched[n].pid = fork_process(n);
	int running_cnt = n;
	int running_idx = -1;
	while(running_cnt){
		//fprintf(stderr, "%d:\n", t_now);
		if(running_idx != -1 && sched[running_idx].T == 0){
			waitpid(sched[running_idx].pid, NULL, 0);
			fprintf(stderr, "%d: child %d terminated\n", t_now, running_idx);
			fflush(stderr);
			fsync(2);
			read(up_pipe[running_idx][0], &(sched[running_idx].st), sizeof(sched[running_idx].st));
			read(up_pipe[running_idx][0], &(sched[running_idx].et), sizeof(sched[running_idx].et));
			running_idx = -1;
			running_cnt--;
		}
		for(int i = 0; i < n; i++){
			if(sched[i].R == t_now){
				sched[i].pid = fork_process(i);
				fprintf(stderr, "%d: child %d created\n", t_now, i);
				fflush(stderr);
				fsync(2);
			}
		}
		//fprintf(stderr, "%d: selecting next process...\n", t_now);
		int next = next_process(running_idx, policy);
		//fprintf(stderr, "%d: next process %d selected!\n", t_now, next);
		if(next != running_idx){
			fprintf(stderr, "%d: child %d context switchs to child %d\n", t_now, running_idx, next);
			fflush(stderr);
			fsync(2);
			running_idx = next;
			t_last = t_now;
		}
		char buf = 'a';
		if(running_idx != -1){
			int wrp = write(down_pipe[running_idx][1], &buf, 1);
			if(wrp < 0)perror("write");
		}else{
			int wrp = write(down_pipe[n][1], &buf, 1);
			if(wrp < 0)perror("write");
		}
		//fprintf(stderr, "%d: writing OK\n", t_now);
		run_one_unit();
		t_now++;
		if(running_idx != -1){
			sched[running_idx].T--;
		}
	}
	for(int i = 0; i < n; i++){
		printf("%s %d\n", sched[i].N, sched[i].pid);
	}
	char cmd[256] = {};
	for(int i = 0; i < n; i++){
		sprintf(cmd, "echo [Project1] %d %ld.%09ld %ld.%09ld|sudo tee /dev/kmsg > /dev/null", sched[i].pid, sched[i].st.tv_sec, sched[i].st.tv_nsec, sched[i].et.tv_sec, sched[i].et.tv_nsec);
		system(cmd);
	}
	kill(sched[n].pid, 9);
	wait(NULL);
	return 0;
}
