#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include "Machine.h"
#include "VirtualMachine.h"
#include <vector>
#include <queue>
#include <iostream>
#include <algorithm>
#include <string.h>
#include <map>

using namespace std;
extern "C"{
    
volatile int tickCounter = 0;
volatile int tickConversion;
volatile bool mainCreated = false;
volatile int mutexIDCounter = 0;
volatile int threadIDCounter = 2;
volatile int memPoolIDCounter = 2;
const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;
//bool * byteArray;
void * byteArrayStart;
void * heapArrayStart;
class Mutex;
int sharedSize;
int heapSize;

class memPool{
public:
    void * initialptr;
    TVMMemorySize size;
    TVMMemoryPoolID ID;
    bool * boolArray;
    map <void *, int> ptrSizes;
    memPool();
    memPool(void * init, TVMMemorySize siz, TVMMemoryPoolID eyedee);
    
};

class TCB{
public:
    SMachineContext context; 
    TVMThreadID ID; 
    TVMThreadPriority priority;
    TVMThreadState state;
    TVMMemorySize memSize;
    void * stack;
    TVMThreadEntry entry;
    TVMTick tick;
    bool sleeping;
    int result;
    Mutex * mutex;
    void * param;
    TCB();
    TCB(SMachineContext mContext, TVMThreadID id, TVMThreadPriority prio,
    TVMThreadState stat, TVMMemorySize mSize,TVMThreadEntry entry,void * para);
    //~TCB();
};

class Compare{
public:
    bool operator() (TCB *r,TCB *l){
        return (r->priority < l->priority);
    }
};

class Mutex{
public:
    bool locked;
    TVMMutexID ID;
    TVMThreadID owner;
    TCB * ownerTCB;
    priority_queue <TCB*, vector<TCB*>, Compare > waiting;
    Mutex(TVMMutexID id);
};

vector <TCB*> threads;

vector <Mutex*> mutexes;

vector <memPool*> memoryPools;

priority_queue <TCB*, vector<TCB*>, Compare > ready;

TCB * running;

TVMMainEntry VMLoadModule(const char *module);

void * firstFit(bool * array, int sizeOfArray, int chunk, void * initial, void * object, bool memcopy){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    bool success = true;
    int size = sizeOfArray/64;
    for (int i = 0; i < size; i++)
    {
        if(array[i]){
            if (chunk%64 == 0)
                chunk = (chunk / 64);
            else
                chunk = (chunk / 64)+1;
            for(int j = i; j < chunk+i; j++)
            {
                if (!array[j])
                {
                    success = false;
                    i = j;
                    break;
                }
            }
            if (success)
            {
                if (memcopy)
                    memcpy((char*)initial+(i*64), object, 64*chunk);
                for(int j = i; j < chunk+i; j++)
                {
                    array[j] = false;
                }
                MachineResumeSignals(&sigState);
                return initial+(i*64);
            }
                
            else
                success = true;
        }
    }
    MachineResumeSignals(&sigState);
    return NULL;
}

bool freeByteArray(bool * array, void * initial, void * ptr, int length)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    int check= (char *)ptr - (char *)initial;
    check = check / 64;
    if (array[check] == true)
    {
        MachineResumeSignals(&sigState);
        return false;
    }
    else{
        length = (length / 64);
        for (int i = ((char *)ptr - (char *)initial)/64; i < length+check; i++)
            array[i] = true;
    }
    MachineResumeSignals(&sigState);
    return true;
}

void idleFun(void *param)
{
    MachineEnableSignals();
    TMachineSignalState sigState;
    MachineResumeSignals(&sigState);
    while(1) 
        MachineResumeSignals(&sigState);
    
}

void mainThreadCreate(){            //creates main and idle threads and system memory pool
    TCB * Main;
    TCB * idle;
    SMachineContext idlecontext;
    idle = new TCB();
    idle->entry = idleFun;
    idle->priority = 0;
    idle->state = VM_THREAD_STATE_READY;
    idle->context = idlecontext;
    idle->memSize = 0x10000;
    //idle->stack = malloc(idle->memSize);
    VMMemoryPoolAllocate(0,idle->memSize, (void**)&idle->stack);
    idle->ID = 1;
    MachineContextCreate(&idle->context,idle->entry, idle, 
            NULL,NULL);
    Main = new TCB();
    running = Main; 
    threads.push_back(Main);
    threads.push_back(idle);
    ready.push(idle);
    mainCreated = true;
}

void wrapper(void * param){
    MachineEnableSignals();
    running->entry(param);
    VMThreadTerminate(running->ID);
}

bool queueHelper(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (ready.empty())
        return false;
    if(ready.top()->priority >= running->priority)
        return true;
    else
        return false;
}

bool queueHelper2(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (ready.empty())
        return false;
    if(ready.top()->priority > running->priority)
        return true;
    else
        return false;
}

void scheduler(int code, TCB * thread)
{  
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB * temp = running;
    if (code == 6) //running blocked, run next ready
    {
        running->state = VM_THREAD_STATE_WAITING;
        running = ready.top();
        running->state = VM_THREAD_STATE_RUNNING;
        ready.pop();
        //cout << "Thread ID " << running->ID << " is now running!" << endl;
        MachineContextSwitch(&temp->context,&running->context);
        MachineResumeSignals(&sigState);
    }
    else if (code == 1) //Waiting to ready
    {
        if(queueHelper()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
    else if (code == 7) //waiting to ready for mutex
    {
        if(queueHelper2()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }   
    }
    else if (code == 3 ) //quantum is up, running goes to ready.
    {
        if(queueHelper()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            ready.top()->state = VM_THREAD_STATE_RUNNING;
            running = ready.top();
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
    else if(code == 4){
        thread->state = VM_THREAD_STATE_DEAD;
        running = ready.top();
        ready.pop();
        //cout << "Thread ID " << running->ID << " is now running!" << endl;
        //MachineResumeSignals(&sigState);
        MachineContextSwitch(&temp->context, &running->context); 
        MachineResumeSignals(&sigState);
    }
    else if(code == 5){
        thread->state = VM_THREAD_STATE_READY;
        ready.push(thread);
        if(queueHelper2()){
            running->state = VM_THREAD_STATE_READY;
            ready.push(running);
            thread->state = VM_THREAD_STATE_RUNNING;
            running = thread;
            ready.pop();
            //cout << "Thread ID " << running->ID << " is now running!" << endl;
            //MachineResumeSignals(&sigState);
            MachineContextSwitch(&temp->context, &running->context);
            MachineResumeSignals(&sigState);
        }
    }
}

memPool::memPool(){
    initialptr = NULL;
    size = 0;
    ID = 0;
}

memPool::memPool(void * init, TVMMemorySize siz, TVMMemoryPoolID eyedee)
{
    initialptr = init;
    size = siz;
    ID = eyedee;
    boolArray = new bool[size/64];
    for(int i = 0; i< size/64; i++){
        boolArray[i] = true;
    }
}

TCB::TCB(){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    state = VM_THREAD_STATE_RUNNING;
    MachineContextSave(&context);
    priority = VM_THREAD_PRIORITY_NORMAL;
    //tick = 99999999999999999999999999999999999999999;
    tick = 0;
    sleeping = false;
    result = 0;
    ID = 0;
    MachineResumeSignals(&sigState);
}

TCB::TCB(SMachineContext mContext, TVMThreadID id, TVMThreadPriority prio,
        TVMThreadState stat, TVMMemorySize mSize,TVMThreadEntry mEntry, void * para){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    ID = id;
    priority = prio;
    state = stat;
    memSize = mSize;
    //stack = malloc(mSize);
    VMMemoryPoolAllocate(0,mSize, (void**)&stack);
    context = mContext;
    entry = mEntry;
    //tick = 99999999999999999999999999999999999999999;
    tick = 0;
    sleeping = false;
    result = 0;
    param = para;
    MachineResumeSignals(&sigState);
}

Mutex::Mutex(TVMMutexID id)

{
    locked = false;
    ID = id;
}

void sleepCB(void* callbackTCB){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    tickCounter++;
    for (unsigned i = 0; i < threads.size(); i++)
    {
        if (threads[i]->sleeping && tickCounter >= threads[i]->tick)
        {
            threads[i]->sleeping = false;
            //threads[i]->tick = 99999999999999999999;
            threads[i]->tick = 0;
            threads[i]->state = VM_THREAD_STATE_READY;
            ready.push(threads[i]);
        }

    }
    scheduler(3,running);
    MachineResumeSignals(&sigState);
}

TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, int argc, char *argv[]){
    byteArrayStart = MachineInitialize(sharedsize);
    sharedSize = sharedsize;
    heapSize = heapsize;
    memPool * systempool;
    uint8_t * base;
    base = new uint8_t[heapSize];
    systempool = new memPool((void *)base, heapSize, 0);
    memPool *sharedMemory;
    sharedMemory = new memPool(byteArrayStart, sharedSize, 1);
    memoryPools.push_back(systempool);
    memoryPools.push_back(sharedMemory);
    MachineRequestAlarm(tickms*1000, sleepCB, NULL);
    tickConversion = tickms;
    TVMMainEntry main = VMLoadModule(argv[0]);
    main(argc, argv);
    TVMMainEntry VMUnLoadModule();
    MachineTerminate();

    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickMS(int *tickmsref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!tickmsref)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    *tickmsref = tickConversion;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickCount(TVMTickRef tickref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(!tickref)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    *tickref = tickCounter;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}
TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, 
        TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if(!tid || !entry)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    SMachineContext tempcontext;
    TCB * temp;
    *tid = threadIDCounter;
    temp = new TCB(tempcontext,*tid,prio,VM_THREAD_STATE_DEAD,memsize, entry, param);
    threads.push_back(temp);
    threadIDCounter++;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}  

TCB* findThread(TVMThreadID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < threads.size(); i++){
        if(threads[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return threads[i];
        }
    }
    return NULL;
}

TVMStatus VMThreadDelete(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(foundTCB->state != VM_THREAD_STATE_DEAD){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else 
    {
        vector<TCB *>::iterator pos = find(threads.begin(),threads.end(), foundTCB);
        VMMemoryPoolDeallocate(0,foundTCB->stack);
        threads.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;  
}

TVMStatus VMThreadActivate(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if(foundTCB->state == VM_THREAD_STATE_DEAD){
        MachineContextCreate(&foundTCB->context,wrapper, foundTCB->param, 
                foundTCB->stack,foundTCB->memSize);
        //foundTCB->state = VM_THREAD_STATE_READY;
        //ready.push(foundTCB);
        scheduler(5,foundTCB);
    }
    else
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadTerminate(TVMThreadID thread){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    if(foundTCB->state == VM_THREAD_STATE_DEAD){
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    scheduler(4, foundTCB);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;  
}

TVMStatus VMThreadID(TVMThreadIDRef threadref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!threadref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else
        *threadref = running->ID;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!stateref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TCB* foundTCB = findThread(thread);
    if (!foundTCB)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    *stateref = foundTCB->state;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadSleep(TVMTick tick){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if(tick == VM_TIMEOUT_INFINITE)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(tick == VM_TIMEOUT_IMMEDIATE)
    {
        MachineResumeSignals(&sigState);
        scheduler(3,running);
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    if (running)
    {
        running->tick = tick+tickCounter;
        running->sleeping = true;
    }
    scheduler(6,running);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

memPool* findMemPool(TVMMemoryPoolID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < memoryPools.size(); i++){
        if(memoryPools[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return memoryPools[i];
        }
    }
    return NULL;
}

TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    if(!base || !memory || size == 0)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    memPool * temp;
    *memory = memPoolIDCounter;
    temp = new memPool(base, size, *memory);
    memoryPools.push_back(temp);
    memPoolIDCounter++;
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
    
}

TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else 
    {
        for (int i = 0; i < foundMemPool->size; i += 64)
        {
            if (!foundMemPool->boolArray[i/64])
            {
                MachineResumeSignals(&sigState);
                return VM_STATUS_ERROR_INVALID_STATE;
            }
        }
        vector<memPool *>::iterator pos = find(memoryPools.begin(),memoryPools.end(), foundMemPool);
        memoryPools.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool || !bytesleft)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    *bytesleft = 0;
    for (int i = 0; i < foundMemPool->size; i += 64)
    {
        if (foundMemPool->boolArray[i/64])
            *bytesleft = *bytesleft + 64;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
    
    
}

TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(!pointer || size == 0)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    *pointer = firstFit(foundMemPool->boolArray,foundMemPool->size, size,foundMemPool->initialptr, *pointer, 0);
    if (!*pointer)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    else
    {
        foundMemPool->ptrSizes[*pointer] = size;
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer)
{
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    memPool * foundMemPool = findMemPool(memory);
    if (!foundMemPool)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(!pointer)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    
    if (freeByteArray(foundMemPool->boolArray, foundMemPool->initialptr, pointer, foundMemPool->ptrSizes[pointer]))
    {
        foundMemPool->ptrSizes.erase(pointer);
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    else
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    
}

TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mutexref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    Mutex * temp;
    *mutexref = mutexIDCounter;
    temp = new Mutex(*mutexref);
    mutexIDCounter++;
    mutexes.push_back(temp);
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

Mutex* findMutex(TVMMutexID id){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    for(unsigned i = 0; i < mutexes.size(); i++){
        if(mutexes[i]->ID == id)
        {
            MachineResumeSignals(&sigState);
            return mutexes[i];
        }
    }
    return NULL;
}

TVMStatus VMMutexDelete(TVMMutexID mutex){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(foundMutex->locked)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else 
    {
        vector<Mutex *>::iterator pos = find(mutexes.begin(),mutexes.end(), foundMutex);
        mutexes.erase(pos);
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if(!ownerref)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    else if(!foundMutex->locked)
    {
        *ownerref = VM_THREAD_ID_INVALID;
    }
    else
    {
        ownerref = &foundMutex->owner;
    }
    
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
    
}

TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if (!foundMutex->locked)
    {
        running->mutex = foundMutex;
        foundMutex->owner = running->ID;
        foundMutex->ownerTCB = running;
        foundMutex->locked = true;
    }
    else if (timeout == VM_TIMEOUT_INFINITE)
    {
        foundMutex->waiting.push(running);
//        while (foundMutex->ownerTCB != running)
//        {
//            running->tick = tickCounter + 1;
//            running->sleeping = true;
            scheduler(6, running);
        //}
        running->mutex = foundMutex;
        foundMutex->owner = running->ID;
        foundMutex->ownerTCB = running;
        
    }
    else if (timeout == VM_TIMEOUT_IMMEDIATE)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_SUCCESS;
    }
    else
    {
        running->tick = tickCounter + timeout;
        foundMutex->waiting.push(running);
        running->sleeping = true;
        scheduler(6, running);
        if (foundMutex->ownerTCB == running)
        {
            running->mutex = foundMutex;
            foundMutex->owner = running->ID;
            foundMutex->ownerTCB = running;
        }
        else
        {
            MachineResumeSignals(&sigState);
            return VM_STATUS_FAILURE;
        }
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexRelease(TVMMutexID mutex){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    Mutex * foundMutex = findMutex(mutex);
    if (!foundMutex)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_ID;
    }
    else if (!foundMutex->locked)
    {
        MachineResumeSignals(&sigState);
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    else
    {
        foundMutex->ownerTCB->mutex = NULL;
        if (!foundMutex->waiting.empty())
        {
            foundMutex->ownerTCB = foundMutex->waiting.top();
            foundMutex->owner = foundMutex->ownerTCB->ID;
            foundMutex->waiting.pop();
            foundMutex->ownerTCB->state = VM_THREAD_STATE_READY;
            ready.push(foundMutex->ownerTCB);
            scheduler(7,foundMutex->ownerTCB);
        }
        else
            foundMutex->locked = false;
    }
    MachineResumeSignals(&sigState);
    return VM_STATUS_SUCCESS;
}

void CB(void* calldat, int result)
{   
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    TCB * temp2 = (TCB * ) calldat;
    temp2->result = result;
    temp2->state = VM_THREAD_STATE_READY;
    ready.push(temp2);
    scheduler(1, (TCB*) calldat);
    MachineResumeSignals(&sigState);
    
}

TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if (!filename || !filedescriptor)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    int result = 0;
    MachineFileOpen(filename,flags, mode, CB, (void*)running);
    scheduler(6, running);
   result = running->result;
   *filedescriptor = result;
   MachineResumeSignals(&sigState);
   if (result < 0)
       return VM_STATUS_FAILURE;
   else 
       return VM_STATUS_SUCCESS;
}

TVMStatus VMFileClose(int filedescriptor){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    int result = 0;
    MachineFileClose(filedescriptor, CB, (void *)running);
    scheduler(6, running);
    result = running->result;
    MachineResumeSignals(&sigState);
    if (result < 0)
       return VM_STATUS_FAILURE;
    else 
       return VM_STATUS_SUCCESS;
    
}

TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
    TMachineSignalState sigState;
    if (!mainCreated)
        mainThreadCreate();
    if(!data || !length)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    MachineSuspendSignals(&sigState);
    int result = 0;
    if (*length < 512)
    {
        void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,*length, memoryPools[1]->initialptr, data,1);
        MachineFileRead(filedescriptor, ptr, *length, CB, (void*) running);
        scheduler(6, running);
        memcpy(data, ptr, *length);
        freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, *length);
        result = running->result;
        *length = result;
    }
    else
    {
        void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,*length, memoryPools[1]->initialptr, data,1);
        int theLength = *length;
        *length = 0;
        for (int i = 0; i < theLength; i += 512)
        {  
            MachineFileRead(filedescriptor, ptr, *length, CB, (void*) running);
            scheduler(6, running);
            memcpy(data, ptr, *length);
            freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, 512);
            result = running->result;
            *length += result;
        }
    }
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;
        
    
}

TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);
    if (!mainCreated)
        mainThreadCreate();
    if (!data || !length)
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    int result = 0;
    //cout << "Thread ID " << running->ID << " called VMFileWrite!" << endl;
    if (*length < 512)
    {
        void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,*length, memoryPools[1]->initialptr, data,1);
        MachineFileWrite(filedescriptor,ptr, *length, CB, (void*) running);
        scheduler(6, running);
        freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, *length);
        result = running->result;
        *length = result;
    }
    else
    {
        void * ptr = firstFit(memoryPools[1]->boolArray,sharedSize,*length, memoryPools[1]->initialptr, data,1);
        int theLength = *length;
        *length = 0;
        for (int i = 0; i < theLength; i += 512)
        {
            MachineFileWrite(filedescriptor,ptr, 512, CB, (void*) running);
            scheduler(6, running);
            freeByteArray(memoryPools[1]->boolArray, memoryPools[1]->initialptr, ptr, 512);
            result = running->result;
            *length += result;
        }
    }
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
    TMachineSignalState sigState;
    MachineSuspendSignals(&sigState);    
    int result = 0;
    MachineFileSeek(filedescriptor, offset, whence, CB, (void*) running);
    scheduler(6, running);
    result = running->result;
    *newoffset = result;
    MachineResumeSignals(&sigState);
    if (result < 0)
        return VM_STATUS_FAILURE;
    else
        return VM_STATUS_SUCCESS;    
}

} // END OF EXTERN "C" 