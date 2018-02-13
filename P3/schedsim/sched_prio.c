#include "sched.h"

static task_t* pick_next_task_prio(runqueue_t* rq,int cpu) {
    task_t* t=head_slist(&rq->tasks); //List sorted by CPU burst lenght (just pick the first one)
    
    if (t) {
        /* Current is not on the rq*/
        remove_slist(&rq->tasks,t); //borramos de la lista de tareas la tarea
        t->on_rq=FALSE; //booleano de que la tarea ya no esta en la cola
        rq->cur_task=t; //puntero que apunta a la tarea en ejecucion apunta a la tarea que vamos a coger
    }
    
    return t;
}

//comparar las prioridades de las tareas
static int compare_tasks_priority(void *t1,void *t2) {
	task_t* tsk1=(task_t*)t1; //tarea 1
	task_t* tsk2=(task_t*)t2; //tarea2
	return tsk1->prio-tsk2->prio; //Resta de ambas tareas
}

//Encolar una tarea en la cpu
static void enqueue_task_prio(task_t* t,int cpu, int runnable) {
    runqueue_t* rq=get_runqueue_cpu(cpu); //coge la cola de la cpu que estamos
    
    if (t->on_rq || is_idle_task(t)) //si esta vacia la cola
        return;
    
    if (t->flags & TF_INSERT_FRONT){
        //Clear flag
        t->flags&=~TF_INSERT_FRONT;
        sorted_insert_slist_front(&rq->tasks, t, 1, compare_tasks_priority);  //Push task 
      // compara e inserta por prioridad  en la lista segun el orden por delante   
    }
    else
        sorted_insert_slist(&rq->tasks, t, 1, compare_tasks_priority);  //Push task
// inserta por detras de la lista la otra tarea con menos prioridad
    
    t->on_rq=TRUE; //ponemos el booleano de que la tarea esta en cola a true
            
    /* If the task was not runnable before, update the number of runnable tasks in the rq*/
    if (!runnable){
        task_t* current=rq->cur_task;
        //rq->nr_runnable++; //sumamos uno al contador de tareas
        t->last_cpu=cpu; //apuntamos la tarea a la cpu	
        
//replanificamos.
        /* Trigger a preemption if this task has a shorter CPU burst than current */
        if (preemptive_scheduler && !is_idle_task(current) && t->prio<current->prio) {
            rq->need_resched=TRUE; //esta replanificada
            current->flags|=TF_INSERT_FRONT; /* To avoid unfair situations in the event
                                                another task with the same prio wakes up as well*/
        }
    }     
}


static void task_tick_prio(runqueue_t* rq,int cpu){
    
    task_t* current=rq->cur_task;
    //si no tiene tareas la cpu
    if (is_idle_task(current))
        return;
    
    //if (current->runnable_ticks_left==1) 
        //rq->nr_runnable--; // The task is either exiting or going to sleep right now    
}

//roba una tarea a otra cpu, cuando esta vacia ella
static task_t* steal_task_prio(runqueue_t* rq,int cpu){
    task_t* t=tail_slist(&rq->tasks);
    
    if (t) {
        remove_slist(&rq->tasks,t); //borramos de la lista de tareas la tarea t
        t->on_rq=FALSE; //ponemos a false porque la tarea ya no esta en cola
        //rq->nr_runnable--; //quitamos uno al contador de tareas
    }
    return t;    //dev la tarea
}

sched_class_t prio_sched={
    .pick_next_task=pick_next_task_prio,
	.enqueue_task=enqueue_task_prio,
	.task_tick=task_tick_prio,
	.steal_task=steal_task_prio
};
