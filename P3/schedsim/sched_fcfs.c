#include "sched.h"

static task_t* pick_next_task_fcfs(runqueue_t* rq,int cpu) {
	task_t* t=head_slist(&rq->tasks);
    
	if (t) {
        /* Current is not on the rq*/
        	remove_slist(&rq->tasks,t);
        	t->on_rq=FALSE;
        	rq->cur_task=t;
	}
	return t;
}

static void enqueue_task_fcfs(task_t* t,int cpu, int runnable) {
	runqueue_t* rq=get_runqueue_cpu(cpu);

	if (t->on_rq || is_idle_task(t))
        	return;
    
	insert_slist(&rq->tasks,t); //Push task
	t->on_rq=TRUE;
	
	if (!runnable){
		//rq->nr_runnable++;
        	t->last_cpu=cpu;
	}

}


static task_t* steal_task_fcfs(runqueue_t* rq,int cpu){
	task_t* t=tail_slist(&rq->tasks);
    
	if (t) {
        	remove_slist(&rq->tasks,t);
        	t->on_rq=FALSE;
        	//rq->nr_runnable--;
    	}
    	return t;
}

sched_class_t fcfs_sched={
    .pick_next_task=pick_next_task_fcfs,
    .enqueue_task=enqueue_task_fcfs,
    .steal_task=steal_task_fcfs
};
