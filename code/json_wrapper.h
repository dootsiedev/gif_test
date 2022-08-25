#pragma once

const char* rj_string(const rj::Value& value);

//since I am pretty paranoid about compile size, this isn't as bad as it could be.
//so for a basic test example the size went up by 50kb on release and 120kb on debug.
//but it does make the size of the TU about the size of 500kb-1mb.

/*
example:
{
    "value": false,
    "first_table": {
        "array":[
            1,2,3
        ]
    },
    "2d_array":[
        [1,2,3],
        [4,5,6]
    ],
}

//to read
json_context json;
if(!json.open(file.get())) return error;
bool get_bool;
if(json.get_member("value", get_bool)) {
    std::cout << "value: " << get_bool <<'\n';
}
if(json.push_get_object_member("first_table"))
{
    if(json.push_get_array_member("array"))
    {
        int array_buffer[3];
        if(json.get_array(array_buffer, array_buffer + std::size(array_buffer)))
        {
            for(int val: array_buffer)
            {
                std::cout << val << '\n';
            }
        }
        json.pop(); //"array"
    }
    json.pop(); //"first_table"
}


if(json.push_get_array_member("2d_array"))
{
    int size = json.top().GetArray().Size();
    for(int i = 0; i < size; ++i)
    {
        if(json.push_get_array_index(i))
        {
            int array_buffer[3];
			if(json.get_array(array_buffer, array_buffer + std::size(array_buffer)))
			{
				for(int val: array_buffer)
				{
					slogf( "first_table.array: %d\n", val);
				}
			}
            //instead of get_array for more dynamic usage:
            int size2 = json.top().GetArray().Size();
            for(int j = 0; j < size2; ++j)
            {
                int get_number;
                if(json.get_index(i, &get_number)) {
                    std::cout << i << ": "<< get_number <<'\n';
                }
            }
            json.pop(); //i (index)
        }
    }
    json.pop(); //"2d_array"
}

//global state of error, if you want to pass through all your values instead of stopping on 1 error.
if(serr_check_error()) return error;

//writing will check the stack on write(), but for getting you need to manually check the stack.
ASSERT(json.node_stack.empty() && "sanity");

//to write
json_context json;
json.create("in memory");   //must be called
json.set_member("value", true);
json.push_set_object_member("first_table");
    json.push_set_array_member("array");
    int array_buffer[] = {1,2,3};
    json.set_array(array_buffer, array_buffer + std::size(array_buffer));
    //equivilant to:
    //for(int i = 0; i < std::size(array_buffer); ++i) json.push_back(array_buffer[i]);
    json.pop(); //"array"
json.pop() //"first_table"

json.push_set_array_member("2d_array");
    int 2d_array_buffer[][] = {{1,2,3},{4,5,6}};
    for(int i = 0; i < std::size(2d_array_buffer); ++i)
    {
        json.push_set_array_index(i);
        json.set_array(2d_array_buffer[i], 2d_array_buffer[i] + std::size(2d_array_buffer[i]));
        json.pop(); //i (index)
    }
json.pop(); //"2d_array"

//only if you enable the flag for errors on overwrites
if(serr_check_error()) return error;

if(!json.write(file)) return error;

STRING VALUE CAVEAT!!!!
all functions that relate to strings must use std::string_view AND the functions have _string appended, like get_member_string
note that strings used for both keys and values by default are referenced, 
and if the lifetime needs to be extended you must use JSON_KEY_COPY or JSON_VALUE_COPY in flags on setters

if you want to get a value optionally, but you still need to check for conversion errors, use JSON_OPTIONAL on getters
if you want to optionally get a value with with any more than 1 type, 
then you should use rapidjson directly by using top(), this wrapper was never meant to replace rapidjson, it is just for non ASSERT errors.
*/

enum json_set_flag
{
	JSON_SET_DEFAULT = 1,

	// copying the key or value is only for strings,
	// if you create a temporary std::string/buffer, and
	// the the string's lifetime is shorter than
	// the document's write(), you could fix that with copy.
	JSON_COPY_KEY = 2,
	JSON_COPY_VALUE = 4,

	// will report an error if you are overwriting a value.
    // problem: there is no way to detect the error by return, you just need to check serr.
    // this will not print ERROR or WARNING (like other errors), 
    // so you can use this as a warning and just eat the error.
    // TODO: does not apply to arrays, but it should...
	JSON_SET_OVERWRITE_ERROR = 8,

	JSON_SET_MASK =
		(JSON_SET_DEFAULT | JSON_COPY_KEY | JSON_COPY_VALUE | JSON_SET_OVERWRITE_ERROR),
};

enum json_get_flag
{
	// gets
	JSON_GET_DEFAULT = 16,

	// OPTIONAL will make "not found" skip setting error state
	// but "bad convert" will still set the error (use lookup_).
	JSON_OPTIONAL = 32,

	JSON_GET_MASK = (JSON_GET_DEFAULT | JSON_OPTIONAL)
};


//this wrapper isn't 100% safe, but the benefit it offers over rapidjson is
//that it prints good errors.
class json_context{
public:
    struct json_node
    {
        //either an object or array
        rj::Value data;
        //if key is kNullType, use the index
        rj::Value key;
        //if you are using a getter, this will be starting at -1 = 0, -2 = 1, etc
        //and a setter must push_back and no gaps can be made.
        int index;
        template <class T, class K>
        json_node(T&& data_, K&& key_, int index_)
        :data(std::move(data_)), key(std::move(key_)), index(index_)
        {}
    };
    
    rj::Document rj_doc;
    //a stack of nested arrays[] and objects{}, it isn't better than the direct rapidjson syntax, but this offers error information.
    //unique_ptr isn't fast, but the problem is that rapidjson::Value really hates being in a container...
    //maybe using "friend" I can use the private copy constructor, but I don't know if that is safe...
    //a solution that I know works, but it is 100% bad practice, is to use a char buffer to hold rj::Value, and using the placement new / manual destructor
    std::vector<std::unique_ptr<json_node>> node_stack;
    //I don't want to make a copy of the file info, 
    //but it is the most sane option because after the call to open() 
    //I want to allow the file to be closed (along with it's info)
    std::string file_info;
    //TODO: context_flags probably not implemented, and I don't check the GET/SET masks
    int context_flags = 0;

    //returns NULL if not found, prints errors based on flags.
    rj::Value* internal_find_member(const char* function, std::string_view key, int flags);
    rj::Value* internal_find_index(const char* function, int index, int flags);
    void internal_print_convert_error(const char* function, std::string_view key, rj::Value* value, const char* expected);

    //prints the parent path of the node_stack
    //fun fact: the reason why I use a string_view key even though you can 
    //easily combine the path using printf formatting (I do this for indexes)
    //is because I don't want to print a string_view into printf because it prefers C-strings, but string_view supports non c-strings...
    std::string dump_path(std::string_view key = std::string_view());

    // returns the top array/object in rapidjson's API
    // returns the document if nothing is pushed.
    // mainly for arrays, use this to call:
    // json.top().GetArray().Size()
    // json.top().GetArray().Clear()
    // json.top().GetArray().Reserve(size, json.rj_doc.Get_Allocator())
    // json.top().GetArray().PushBack()
    // for(auto& X: json.top().GetArray())
    // for(auto& X: json.top().GetObject())
    rj::Value& top()
    {
        return (node_stack.empty() ? rj_doc : node_stack.back()->data);
    }

    //returns false if an error occurs.
    bool open(RWops* file);

    //returns false if an error occured.
    bool write(RWops* file);

    //you must call this because by default the document is empty, but it must hold a root object
    //the reason why I don't use a constructor is because can you use rapidjson's API to have a weird root
    void create(const char* info = "<unspecified>");

    void rename(const char* info);

    void set_flags(int new_flags);

    ///
    /// NODES -------------------------------
    ///

    //every push must be matched with a pop
    void pop();

    ///
    /// GET NODES -------------------------------
    ///
    
    //array/object refers to creating an array or object, 
    //member/index refers to if the parent is an array(index) or object(member)
    bool push_get_array_member(std::string_view key, int flags = JSON_GET_DEFAULT);
    bool push_get_array_index(int index, int flags = JSON_GET_DEFAULT);
    bool push_get_object_member(std::string_view key, int flags = JSON_GET_DEFAULT);
    bool push_get_object_index(int index, int flags = JSON_GET_DEFAULT);

    ///
    /// SET NODES -------------------------------
    ///
    
    void push_set_array_member(std::string_view key, int flags = JSON_SET_DEFAULT);
    void push_set_array_index(int index, int flags = JSON_SET_DEFAULT);
    void push_set_object_member(std::string_view key, int flags = JSON_SET_DEFAULT);
    void push_set_object_index(int index, int flags = JSON_SET_DEFAULT);


    ///
    /// SETTERS -------------------------------
    ///

    //the key is referenced, to copy use JSON_COPY_KEY, likewise for JSON_COPY_VALUE
    void set_member(std::string_view key, rj::Value value, int flags = JSON_SET_DEFAULT);
    void set_index(int index, rj::Value value, int flags = JSON_SET_DEFAULT);
    void push_back(rj::Value value, int flags = JSON_SET_DEFAULT);

    template <class T>
    void set_member(std::string_view key, T&& value, int flags = JSON_SET_DEFAULT)
    {
        set_member(key, rj::Value(value), flags);
    }

    template <class T>
    void set_index(int index, T&& value, int flags = JSON_SET_DEFAULT)
    {
        set_index(index, rj::Value(value), flags);
    }

    template <class T>
    void push_back(T&& value, int flags = JSON_SET_DEFAULT)
    {
        push_back(rj::Value(value), flags);
    }

    //note that this will append the existing array.
    template<class T>
	void set_array(T start, T end, int flags = JSON_SET_DEFAULT)
    {
        while(start != end)
        {
            push_back(*start, flags);
            ++start;
        }
    }

    //I hate this since it ruins the consistancy, but this is the only way it works without template shenanigans
    void set_member_string(std::string_view key, std::string_view value, int flags = JSON_SET_DEFAULT)
    {
        set_member(key, rj::Value(rj::StringRef(value.data(), value.size())), flags);
    }
    void set_index_string(int index, std::string_view value, int flags = JSON_SET_DEFAULT)
    {
        set_index(index, rj::Value(rj::StringRef(value.data(), value.size())), flags);
    }
    void push_back_string(std::string_view value, int flags = JSON_SET_DEFAULT)
    {
        push_back(rj::Value(rj::StringRef(value.data(), value.size())), flags);
    }
    template<class T>
	void set_array_string(T start, T end, int flags = JSON_SET_DEFAULT)
    {
        while(start != end)
        {
            push_back_string(*start, flags);
            ++start;
        }
    }

    ///
    /// GETTERS -------------------------------
    ///
    template<class T>
    bool get_member(std::string_view key, T &value, int flags = JSON_GET_DEFAULT)
    {
        rj::Value* ret = internal_find_member(__FUNCTION__, key, flags);
        if(ret != NULL)
        {
            using UN_REF = typename std::remove_reference<T>::type;
            //it is OK to cast a int to a double.
            if(!ret->Is<UN_REF>() && !(rj::Value(value).IsDouble() && ret->IsInt()))
            {
                internal_print_convert_error(__FUNCTION__, key, ret, rj_string(rj::Value(value)));
            }
            else
            {
                value = ret->Get<T>();
                return true;
            }
        }
        return false;
    }
    
    bool get_member_string(std::string_view key, std::string_view &value, int flags = JSON_GET_DEFAULT)
    {
        rj::Value* ret = internal_find_member(__FUNCTION__, key, flags);
        if(ret != NULL)
        {
            if(!ret->IsString())
            {
                internal_print_convert_error(__FUNCTION__, key, ret, "string");
            }
            else
            {
                value = std::string_view(ret->GetString(), ret->GetStringLength());
                return true;
            }
        }
        return false;
    }

    template<class T>
    bool get_index(int index, T &value, int flags = JSON_GET_DEFAULT)
    {
        rj::Value* ret = internal_find_index(__FUNCTION__, index, flags);
        if(ret != NULL)
        {
            using UN_REF = typename std::remove_reference<T>::type;
            //it is OK to cast a int to a double.
            if(!ret->Is<UN_REF>() && !(rj::Value(value).IsDouble() && ret->IsInt()))
            {
                internal_print_convert_error(__FUNCTION__, std::to_string(index), ret, rj_string(rj::Value(value)));
            }
            else
            {
                value = ret->Get<T>();
                return true;
            }
        }
        return false;
    }

    bool get_index_string(int index, std::string_view &value, int flags = JSON_GET_DEFAULT)
    {
        rj::Value* ret = internal_find_index(__FUNCTION__, index, flags);
        if(ret != NULL)
        {
            if(!ret->IsString())
            {
                internal_print_convert_error(__FUNCTION__, std::to_string(index), ret, "string");
            }
            else
            {
                value = std::string_view(ret->GetString(), ret->GetStringLength());
                return true;
            }
        }
        return false;
    }


    //designed for arrays of fixed size.
    template<class T>
	bool get_array(T start, T end, int flags = JSON_GET_DEFAULT)
    {
        int size = top().GetArray().Size();
        if(size != (end - start))
        {
            serrf("json_context::%s Error in `%s`: incorrect array size: \"%s\", size: %d, expected: %d\n", 
                __FUNCTION__, 
                file_info.c_str(), 
                dump_path().c_str(), 
                size, 
                static_cast<int>(end - start));
            return false;
        }
        
        for(T cur = start; cur != end; ++cur)
        {
            if(!get_index((cur - start), *cur, flags))
            {
                return false;
            }
        }
        return true;
    }

    //MUST BE AN ARRAY OF std::string_view
    template<class T>
	bool get_array_string(T start, T end, int flags = JSON_GET_DEFAULT)
    {
        int size = top().GetArray().Size();
        if(size != (end - start))
        {
            serrf("json_context::%s Error in `%s`: incorrect array size: \"%s\", size: %d, expected: %d\n", 
                __FUNCTION__, 
                file_info.c_str(), 
                dump_path().c_str(), 
                size, 
                static_cast<int>(end - start));
            return false;
        }
        
        for(T cur = start; cur != end; ++cur)
        {
            if(!get_index_string((cur - start), *cur, flags))
            {
                return false;
            }
        }
        return true;
    }

    //requires a container that has push_back and ::ValueType (eg: std::vector)
    template<class T>
	bool get_dynamic_array(T& container, int flags = JSON_GET_DEFAULT)
    {
        typename T::value_type k;
        int size = top().GetArray().Size();
        for(int i = 0; i < size; ++i)
        {
            if(!get_index(i, k, flags))
            {
                return false;
            }
            container.push_back(k);
        }
        return true;
    }

    //requires a container that has push_back
    //MUST BE AN ARRAY OF std::string_view
    template<class T>
	bool get_dynamic_array_string(T& container, int flags = JSON_GET_DEFAULT)
    {
        std::string_view k;
        int size = top().GetArray().Size();
        for(int i = 0; i < size; ++i)
        {
            if(!get_index_string(i, k, flags))
            {
                return false;
            }
            container.push_back(k);
        }
        return true;
    }
};
