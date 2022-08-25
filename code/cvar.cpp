#include "global.h"
#include "cvar.h"

#include "SDL_wrapper.h"
#include "json_wrapper.h"
#include "mini_tools.h"

#include <cmath>

static bool cvar_startup_flag = false;

//when you parse the immediate command line arguments, you can change the cv_cfg_file before the file is loaded.
static cvar& cv_config_file = register_cvar_string(
	"cv_config_file", "config.json", "the location of the configuration file", CVAR_STARTUP);

cvar::cvar(std::string&& string_, const char* comment_, int flags_)
		: string(std::move(string_)), value(NAN), comment(comment_), flags(flags_)
{
}
cvar::cvar(double value_, const char* comment_, int flags_)
	: string(better_to_string(value_)), value(value_), comment(comment_), flags(flags_)
{
}

void cvar::set_string(std::string_view string_)
{
	ASSERT(std::isnan(value) && "not a string");
	if(flags == CVAR_DISABLED)
	{
		slog("warning: cvar is disabled\n");
		return;
	}
	if((flags == CVAR_STARTUP) && cvar_startup_flag)
	{
		//I want to include the name, but the std::map holds the name...
		slog("restart is required for cvar to take affect\n");
	}
	string = string_;
}

void cvar::set_value(double value_)
{
	ASSERT(!std::isnan(value) && "not a value");
	if(flags == CVAR_DISABLED)
	{
		slog("cvar is disabled\n");
		return;
	}
	if((flags == CVAR_STARTUP) && cvar_startup_flag)
	{
		//I want to include the name, but the std::map holds the name...
		slog("restart is required for cvar to take affect\n");
	}
	value = value_;
	string = better_to_string(value_);
}

bool cvar::is_string()
{
	return std::isnan(value);
}

// std::map is perfect since it sorts all cvars for you.
std::map<std::string, cvar>& get_convars()
{
	// I think I need to keep this in a function because of initialization order.
	static std::map<std::string, cvar> g_convars;
	return g_convars;
}

// all registration should be done with global const char*
cvar& internal_register_cvar_string(const char* name, const char* string, const char* comment, int flags, const char* file, int line)
{
	auto [it, success] = get_convars().try_emplace(name, string, comment, flags);
	//ASSERT(success && "cvar already registered");
	if(!success)
	{
		serrf("ERROR register_cvar_string: cvar already registered: `%s`\n"
		"File: %s, Line %d\n", name, file, line);
	}
	return it->second;
}
cvar& internal_register_cvar_value(const char* name, double value, const char* comment, int flags, const char* file, int line)
{
	auto [it, success] = get_convars().try_emplace(name, value, comment, flags);
	//ASSERT(success && "cvar already registered");
	if(!success)
	{
		serrf("ERROR register_cvar_value: cvar already registered: `%s`\n"
		"File: %s, Line %d\n", name, file, line);
	}
	return it->second;
}

bool cvar_args(int argc, char** argv)
{
	for(int i = 0; i < argc; ++i)
	{
		if(argv[i][0] != '+')
		{
			serrf("ERROR: cvar option must start with a '+': %s\n", argv[i]);
			return false;
		}
		
		const char* name = argv[i] + 1;
		cvar *cv = cvar_find(name);
		if(cv == NULL)
		{
			serrf("ERROR: cvar not found: %s\n", name);
			return false;
		}

		//go to next argument.
		i++;
		if(i >= argc)
		{
			serrf("ERROR: cvar assignment missing: %s\n", name);
			return false;
		}

		if(cv->get_flags() == CVAR_DISABLED)
		{
			slogf("warning: cvar disabled: %s\n", name);
			continue;
		}

		if(cv->is_string())
		{
			cv->set_string(argv[i]);
		}
		else
		{
			/* I want to use this but it I think it had a problem or something with sanitizer or something on gcc.
			int len = strlen(argv[i]);
			float value;
			auto [p, ec] = std::from_chars(argv[i], argv[i]+len, value);
			//if there is junk at the end, set as a string.
			if(ec == std::errc() || p == argv[i]+len)
			{
				cv->set_value(value);
			}
			else*/
			char *end_ptr;
            errno = 0;
			double value = strtod(argv[i], &end_ptr);
            if(errno == ERANGE)
            {
                serrf("Error: cvar value out of range: +%s %s\n", name, argv[i]);
            }
            else if(end_ptr == argv[i])
            {
                serrf("Error: cvar value not valid numeric input: +%s %s\n", name, argv[i]);
            }
            else
            {
                if(*end_ptr != '\0')
                {
                    slogf("warning: cvar value extra characters on input: +%s %s\n", name, argv[i]);
                }
                cv->set_value(value);
                slogf("%s = %s\n", name, cv->get_string().c_str());
            }
		}
	}

	cvar_startup_flag = true;
	return !serr_check_error();
}

bool cvar_config_file()
{
	if(cv_config_file.get_string().empty())
	{
		return true;
	}
	const char* config_path = cv_config_file.get_string().c_str();
	Unique_RWops config_file(Unique_RWops_OpenFS(config_path, "rb"));
	if(!config_file)
	{
		//eat the error, config files aren't required.
		serr_get_error();
		return true;
	}

	json_context json;
	if(!json.open(config_file.get()))
	{
		return false;
	}

	for(const auto & it: json.top().GetObject())
	{
		std::string_view key_name(it.name.GetString(), it.name.GetStringLength());
		cvar *cv = cvar_find(it.name.GetString());
		if(cv == NULL)
		{
			serrf("%s: cvar not found: %s\n", config_path, it.name.GetString());
			return false;
		}
		if(cv->get_flags() == CVAR_DISABLED)
		{
			slogf("warning: cvar disabled: %s\n", it.name.GetString());
			continue;
		}
		if(cv->is_string())
		{
			std::string_view output;
			if(json.get_member_string(key_name, output))
			{
				cv->set_string(output);
			}
		}
		else
		{
			double output;
			if(json.get_member(key_name, output))
			{
				cv->set_value(output);
			}
		}
		slogf("%s = %s\n", it.name.GetString(), cv->get_string().c_str());
	}

	return !serr_check_error();
}
