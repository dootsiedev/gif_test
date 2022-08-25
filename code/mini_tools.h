#pragma once

//scale reduces the decimal places, eg: 0.1 = 1 decimal place, 0.001 = 3 decimal places.
inline std::string better_to_string(double t, double scale = 0)
{
	// obviously this isn't the most efficient approach.
	std::string str = (scale == 0 ? std::to_string(t) : std::to_string((int)(t / scale) * scale));
	int offset = 1;
	auto found = str.find_last_not_of('0');
	if(found == str.find('.'))
	{
		offset = 0;
	}
	str.erase(found + offset, std::string::npos);
	return str;
}

// a note about the event listener / observer system, I could have made this into one macro
// but the reason why I didn't is because I don't want the macro to do too much,
// and because you can define a listener in a header, and observer in a source file.

// name, function name, parameter type.
// you inherit this to the object and override the function,
// then you can hook to the matching observer.
// this is limited to just one parameter because it's simple.
// I could use constexpr auto to make passing the function name into 
// INIT_EVENT_OBSERVER redundant, but I don't think it purifies this.
#define INIT_EVENT_LISTENER(listener_name, function_name, type) \
	class listener_name                                         \
	{                                                           \
	  public:                                                   \
		typedef type MACRO_LISTENER_PARAM_TYPE;                 \
		int listener_name##_MACRO_LISTENER_INDEX = -1;          \
		virtual void function_name(type) = 0;                   \
		virtual ~listener_name() = default;                     \
	}

// observer name, listener event name, and function name
// note that the order of listerners triggered is random.
// you cannot remove a listener while in the same callback.
// functions:
//-trigger(param) calls all listeners with the parameter passed in
//-add_listener(pointer to listener)
//-remove_listener(pointer to listener)
//-clear() removes all listeners.
#define INIT_EVENT_OBSERVER(observer_name, listener_name, function_name)                  \
	class observer_name                                                                   \
	{                                                                                     \
		std::vector<listener_name*> calls;                                                \
                                                                                          \
	  public:                                                                             \
		void trigger(listener_name::MACRO_LISTENER_PARAM_TYPE param) const                \
		{                                                                                 \
			for(listener_name * event : calls)                                            \
				event->function_name(param);                                              \
		}                                                                                 \
		void add_listener(listener_name* listener)                                        \
		{                                                                                 \
			ASSERT(listener->listener_name##_MACRO_LISTENER_INDEX == -1 && #observer_name \
				   "::add_listener listener must be inactive");                           \
			listener->listener_name##_MACRO_LISTENER_INDEX = calls.size();                \
			calls.push_back(listener);                                                    \
		}                                                                                 \
		void remove_listener(listener_name* listener)                                     \
		{                                                                                 \
			ASSERT(listener->listener_name##_MACRO_LISTENER_INDEX != -1 && #observer_name \
				   "::remove_listener listener must be active");                          \
			calls.back()->listener_name##_MACRO_LISTENER_INDEX =                          \
				listener->listener_name##_MACRO_LISTENER_INDEX;                           \
			calls.at(listener->listener_name##_MACRO_LISTENER_INDEX) = calls.back();      \
			listener->listener_name##_MACRO_LISTENER_INDEX = -1;                          \
			calls.pop_back();                                                             \
		}                                                                                 \
		void clear()                                                                      \
		{                                                                                 \
			calls.clear();                                                                \
		}                                                                                 \
	}
