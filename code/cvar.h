#pragma once


//I use the name cvar as a reference to doom's cvar.
//the interface is dumb, but this simplifies dumb configuration settings
//which might not be relevant for anything other than developement
//unlike doom I just use cvars, no fancy console with bindings, functions, and actions.

//these might not be flags, I might choose to make them just enums.
enum CVAR_FLAGS
{
	//CVAR_DEFAULT is for values that you can change at any time and it will take effect.
	CVAR_DEFAULT = 0,
	// CVAR_STARTUP is to document cvars that require a restart to take effect.
	// it is technically possible to write robust code to avoid this flag completely, but it's difficult.
	CVAR_STARTUP = 1,
	// CVAR_CACHED is when it is possible to change during runtime, but it requires additional code to make changes occur.
	// for example the window size needs to be committed, you cannot just change the value and magically resize the screen.
	CVAR_CACHED = 2,
	// instead of saying this cvar doesn't exist, say this cvar is disabled.
	CVAR_DISABLED = 3,
	MAX_CVAR_FLAGS
};

//this is inspired by quake's cvar.
class cvar
{
public:
	//no default constructor.
	cvar() = delete;

	explicit cvar(std::string&& string_, const char* comment_, int flags_);
	explicit cvar(double value_, const char* comment_, int flags_);

	// I hate getters / setters with a passion, but here it feels right.
	void set_string(std::string_view string_);

	void set_value(double value_);

	bool is_string();

	const std::string &get_string() const
	{
		return string;
	}

	double get_value() const
	{
		return value;
	}

	const char* get_comment() const
	{
		return comment;
	}

	int get_flags() const
	{
		return flags;
	}

  private:
	std::string string;
	double value;
	const char* comment;
	int flags;
};

// std::map is perfect since it sorts all cvars for you.
std::map<std::string, cvar>& get_convars();

// all registration should be done with global const char*
MYNODISCARD cvar& internal_register_cvar_string(const char* name, const char* string, const char* comment, int flags, const char* file, int line);
MYNODISCARD cvar& internal_register_cvar_value(const char* name, double value, const char* comment, int flags, const char* file, int line);
#define register_cvar_string(name, string, comment, flags) internal_register_cvar_string(name, string, comment, flags, __FILE__, __LINE__)
#define register_cvar_value(name, value, comment, flags) internal_register_cvar_value(name, value, comment, flags, __FILE__, __LINE__)

template<class StringType>
cvar* cvar_find(StringType&& name)
{
	auto it = get_convars().find(name);
	if(it == get_convars().end()) return NULL;
	return &it->second;
}

//+cv_name_of_cvar 1 +anyname "abc"
MYNODISCARD bool cvar_args(int argc, char** argv);

MYNODISCARD bool cvar_config_file();
