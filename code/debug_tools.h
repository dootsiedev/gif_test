#pragma once
//#define NO_THREADS

void debug_stacktrace_string(std::string& output, int skip = 0);

#ifndef NO_THREADS

/*
you must make the thread function use this format
    void test_function(debug_thread* context)
    {
        debug_thread_raii context_raii(context);
        while(true)
        {
            context->pulse();
        }
    }
created with:
    debug_thread test_thread("test", test_function);
or
    debug_thread loop_thread;
    loop_thread.userdata = ...;
    loop_thread.run("mainloop", loop_function);
	
and for checking for timeouts (this is ugly because I copy-pasted it for reference, because I have no other examples because I stopped using threads atm):
	static bool warn_once = false;
    if(!warn_once && !loop_thread.check_pulse_ms(static_cast<int>(cv_thread_timeout_ms.get_value())))
    {
        //if the thread timed out, thats bad, but maybe theres a possibility that it is working
        //so I don't escape. You could attempt to exit normally after closing the popup
        serr("Warning this is only shown once.\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL);
        warn_once = true;
    }
and if you are joining a thread (and this shows checking if the thread has set a signal for a graceful exit or not)
    //join the thread (this will print a stacktrace on windows of the timed out thread)
    if(!loop_thread.timed_join(static_cast<int>(cv_thread_timeout_ms.get_value())))
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", serr_get_error().c_str(), window);
        //there is no graceful way of continuing.
        exit(1);
    }
    std::string errors = loop_thread.get_errors();
    if(errors.empty() && loop_state == LOOP_THREAD_ERROR)
    {
        serrf("Exited by due to thread error, but no errors in the thread were made...\n");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", serr_get_error().c_str(), NULL);
    }
    else if(!errors.empty())
    {
        if(loop_state != LOOP_THREAD_ERROR){
            //TODO: instead of a single flag for multiple threads to signal that they had an error or not
            //because a thread might overwrite the other threads error in a once in a blue moon situation.
            //but it doesn't matter because you should get 2 message boxes with the correct errors, 
            //but you will get a bug saying that the error did not set the error state for one thread.
            serrf("\nThread exited normally but errors were made...\n");
        }
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errors.c_str(), window);
    }

 * 
 * note that you can use a lambdas function as the function, but it still requires the context.
 * */
class debug_thread
{
public:
    std::thread current_thread;

    //to print into error messages
    //must be a global string.
    const char* name;

    //this is useless because of lambdas captures. 
    //but if you hate lambdas, this is an alternative.
    void* userdata = NULL;

    //a reference to the thread's serr buffer, 
    //only set after debug_thread_start is called.
    //not thread safe, only read after joining successfully.
    std::shared_ptr<std::string> serr_buffer;

    std::atomic<TIMER_U> pulse_time;

    //I need to check if end() was called.
    //but it doesn't care about start()
    //because I use this to see if it is time to join.
    std::atomic_bool exited;

    debug_thread()
    : name(NULL)
    , pulse_time(TIMER_U())
    , exited(true)
    {

    }

    //the name must be a global string
    //the function must use debug_thread* as the first parameter.
    template<class T>
    debug_thread(const char* name_, T && function)
    : name(name_)
    , pulse_time(timer_now())
    , exited(false)
    {
        //I want to initialize this after exited = false, I want to put this into the initializer thingy, but I don't really know how the order works.
        current_thread = std::thread(std::move(function), this);
    }

    template<class T>
    void run(const char* name_, T && function)
    {
        name = name_;
        pulse_time.store(timer_now());
        exited = false;
        
        current_thread = std::thread(std::move(function), this);
    }

    //start, pulse and end are called within the thread.
    void start(){serr_buffer = internal_get_serr_buffer();}
    void pulse(){pulse_time.store(timer_now());}
    void end(){exited = true;}

    //prints an error if timed out.
    //pass -1 to ignore the timer.
    MYNODISCARD bool check_pulse_ms(int timeout_ms);

    //returns false if timed out.
    //if timed out it is best to read serr, and immediatly exit() because nothing can safely stop a thread.
    //if successful check get_errors() if there were errors inside of the thread.
    MYNODISCARD bool timed_join(int ms_time);

    //DONT CALL THIS BEFORE JOINING
    std::string get_errors();
};

//for safety.
struct debug_thread_raii
{
    debug_thread* context;
    debug_thread_raii(debug_thread* context_)
    : context(context_)
    {
        context->start();
    }
    ~debug_thread_raii()
    {
        context->end();
    }
};

#endif
