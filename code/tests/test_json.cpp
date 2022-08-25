#include "../global.h"
#include "../SDL_wrapper.h"
#include "../json_wrapper.h"

#include <sstream>
#include <limits.h>
#include <float.h>

#define EPSILON 0.00001   // floating point tolerance should use std::::numeric_limits<T>::epsilon
#define FLOAT_EQ(x,v) (((v - EPSILON) < x) && (x <( v + EPSILON)))

struct member_stored_data{
    bool test_true = true;
    bool test_false = false;
    int test_int = 41;
    int test_int_max = INT_MAX;
    int test_int_min = INT_MAX;
    double test_double = 0.123456;
    double test_double_max = 999.999;//DBL_MAX;  I am not dealing with mins and max because it works, just that it's impossible to compare without comparing strings
    double test_double_min = -999.999;
    std::string_view test_string = "abcdef";

    bool compare_to_result(const member_stored_data& result) const
    {
        if(test_true != result.test_true)
        {
            serrf("mismatching results: test_true expected: %s, got: %s\n", 
                (test_true ? "true" : "false"), (result.test_true ? "true" : "false"));
            return false;
        }
        if(test_false != result.test_false)
        {
            serrf("mismatching results: test_false expected: %s, got: %s\n", 
                (test_false ? "true" : "false"), (result.test_false ? "true" : "false"));
            return false;
        }
        if(test_int != result.test_int)
        {
            serrf("mismatching results: test_int expected: %d, got: %d\n", test_int, result.test_int);
            return false;
        }
        if(test_int_max != result.test_int_max)
        {
            serrf("mismatching results: test_int_max expected: %d, got: %d\n", test_int_max, result.test_int_max);
            return false;
        }
        if(test_int_min != result.test_int_min)
        {
            serrf("mismatching results: test_int_min expected: %d, got: %d\n", test_int_min, result.test_int_min);
            return false;
        }
        if(!FLOAT_EQ(test_double, result.test_double))
        {
            serrf("mismatching results: test_double expected: %g, got: %g\n", test_double, result.test_double);
            return false;
        }
        if(!FLOAT_EQ(test_double_max, result.test_double_max))
        {
            serrf("mismatching results: test_double_max expected: %g, got: %g\n", test_double_max, result.test_double_max);
            return false;
        }
        if(!FLOAT_EQ(test_double_min, result.test_double_min))
        {
            serrf("mismatching results: test_false expected: %g, got: %g\n", test_double_min, result.test_double_min);
            return false;
        }
        if(test_string != result.test_string)
        {
            serrf("mismatching results: test_false expected: %s, got: %s\n", test_string.data(), result.test_string.data());
            return false;
        }
        return true;
    }
};

static bool member_read_and_comp(json_context& json, const member_stored_data& write)
{
    member_stored_data read;

    //set dummy values
    read.test_true = false;
    read.test_false = true;
    read.test_int = 0;
    read.test_int_max = 0;
    read.test_int_min = 0;
    read.test_double = 0;
    read.test_double_max = 0;
    read.test_double_min = 0;
    read.test_string = "";
    
    json.get_member("test_true", read.test_true);
    json.get_member("test_false", read.test_false);
    json.get_member("test_int", read.test_int);
    json.get_member("test_int_max", read.test_int_max);
    json.get_member("test_int_min", read.test_int_min);
    json.get_member("test_double", read.test_double);
    json.get_member("test_double_max", read.test_double_max);
    json.get_member("test_double_min", read.test_double_min);
    json.get_member_string("test_string", read.test_string);

    if(serr_check_error())
    {
        return false;
    }
    
    if(!write.compare_to_result(read))
    {
        return false;
    }
    return true;
}


//test members by: insert, compare, write file, read file, compare.
bool test_json_1()
{
    //not a robust file, but just don't write a very large json file.
    //I could have a deque or stringstream based memory stream in RWops, but it's only useful for testing...
    char file_memory[9999];
    Unique_RWops mem_file(Unique_RWops_FromMemory(file_memory, sizeof(file_memory), false, __FUNCTION__));
    if(!mem_file)
    {
        return false;
    }
    const member_stored_data write;

    {
        
        json_context json;
        json.create(mem_file->stream_info);

        json.set_member("test_true", write.test_true);
        json.set_member("test_false", write.test_false);
        json.set_member("test_int", write.test_int);
        json.set_member("test_int_max", write.test_int_max);
        json.set_member("test_int_min", write.test_int_min);
        json.set_member("test_double", write.test_double);
        json.set_member("test_double_max", write.test_double_max);
        json.set_member("test_double_min", write.test_double_min);
        json.set_member_string("test_string", write.test_string);

        if(serr_check_error())
        {
            return false;
        }
        
        if(!member_read_and_comp(json, write))
        {
            return false;
        }

        if(!json.write(mem_file.get()))
        {
            return false;
        }
        
        //shrink the file to fit (sometimes file->seek(0,SEEK_SET) works, but that is because the rest of the stack memory is filled with zeros.)
        int file_length = mem_file->tell();
        mem_file = Unique_RWops_FromMemory(file_memory, file_length, true, __FUNCTION__);
    }
    

    {
        json_context json;
        if(!json.open(mem_file.get()))
        {
            return false;
        }

        if(!member_read_and_comp(json, write))
        {
            return false;
        }
    }

    return true;
}

//test members by loading from a json file
bool test_json_2()
{
    const member_stored_data write;
    std::ostringstream iss;
    {
        iss << "{\n"
        << "\t\"test_true\": "<< (write.test_true ? "true" : "false") << ",\n"
        << "\t\"test_false\": "<< (write.test_false ? "true" : "false") << ",\n"
        << "\t\"test_int\": "<< write.test_int << ",\n"
        << "\t\"test_int_max\": "<< write.test_int_max << ",\n"
        << "\t\"test_int_min\": "<< write.test_int_min << ",\n"
        << "\t\"test_double\": "<< write.test_double << ",\n"
        << "\t\"test_double_max\": "<< write.test_double_max << ",\n"
        << "\t\"test_double_min\": "<< write.test_double_min << ",\n"
        << "\t\"test_string\": \""<< write.test_string << '\"'
        << "\n}";
    }
    std::string file_output(iss.str());
    Unique_RWops mem_file(Unique_RWops_FromMemory(file_output.data(), file_output.size(), true, __FUNCTION__));
    if(!mem_file)
    {
        return false;
    }

    json_context json;
    if(!json.open(mem_file.get()))
    {
        return false;
    }

    if(!member_read_and_comp(json, write))
    {
        return false;
    }
    return true;
}

struct array_stored_data
{
    bool test_bools[4] = {true, false, true, false};
    int test_ints[3] = {43, INT_MIN, INT_MAX};
    //warning: if you make the number larger, precision will be lost, and epsilon won't help
    double test_doubles[4] = {1.1,0.000001, 999.999, -999.999};
    //warning: if you insert escape codes, this will break, including \\n and \n...
    std::string_view test_strings[6] = {"a", "b", "c", "", "n",
     "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};

    void dump() const 
    {
        serr("test_bools:\n");
        for(bool val: test_bools) serrf("\t%s\n", (val ? "true" : "false"));
        serr("test_ints:\n");
        for(int val: test_ints) serrf("\t%d\n", val);
        serr("test_doubles:\n");
        for(double val: test_doubles) serrf("\t%f\n", val);
        serr("test_strings:\n");
        for(const std::string_view& val: test_strings) serrf("\t%s\n", val.data());
    }
};

static bool array_read_and_comp(json_context& json, const array_stored_data& write)
{
    //read fixed
    {
        array_stored_data read;

        //fill stored_data with dummy values before reading.
        bool dummy_bool = false;
        int dummy_int = 0;
        double dummy_double = 0;
        std::string_view dummy_string = "";

        for(bool& val: read.test_bools) val = dummy_bool;
        for(int& val: read.test_ints) val = dummy_int;
        for(double& val: read.test_doubles) val = dummy_double;
        for(std::string_view& val: read.test_strings) val = dummy_string;

        if(json.push_get_array_member("test_bools")){
            json.get_array(read.test_bools, read.test_bools + std::size(read.test_bools));
            json.pop();
        }

        if(json.push_get_array_member("test_ints")){
            json.get_array(read.test_ints, read.test_ints + std::size(read.test_ints));
            json.pop();
        }

        if(json.push_get_array_member("test_doubles")){
            json.get_array(read.test_doubles, read.test_doubles + std::size(read.test_doubles));
            json.pop();
        }

        if(json.push_get_array_member("test_strings")){
            json.get_array_string(read.test_strings, read.test_strings + std::size(read.test_strings));
            json.pop();
        }

        if(serr_check_error())
        {
            return false;
        }
        if(!std::equal(read.test_bools, read.test_bools + std::size(read.test_bools), 
                write.test_bools, write.test_bools + std::size(write.test_bools)))
        {
            serrf("test_bools inequal\n");
            serrf("original:\n");
            for(bool val: write.test_bools) serrf("\t%s\n", (val ? "true" : "false"));
            serr("result:\n");
            for(bool val: read.test_bools) serrf("\t%s\n", (val ? "true" : "false"));
            return false;
        }
        if(!std::equal(read.test_ints, read.test_ints + std::size(read.test_ints), 
                write.test_ints, write.test_ints + std::size(write.test_ints)))
        {
            serrf("test_ints inequal\n");
            serrf("original:\n");
            for(int val: write.test_ints) serrf("\t%d\n", val);
            serr("result:\n");
            for(int val: read.test_ints) serrf("\t%d\n", val);
            return false;
        }
        if(!std::equal(read.test_doubles, read.test_doubles + std::size(read.test_doubles), 
                write.test_doubles, write.test_doubles + std::size(write.test_doubles)))
        {
            serrf("test_doubles inequal\n");
            serrf("original:\n");
            for(double val: write.test_doubles) serrf("\t%f\n", val);
            serr("result:\n");
            for(double val: read.test_doubles) serrf("\t%f\n", val);
            return false;
        }
        if(!std::equal(read.test_strings, read.test_strings + std::size(read.test_strings), 
                write.test_strings, write.test_strings + std::size(write.test_strings)))
        {
            serrf("test_strings inequal\n");
            serrf("original:\n");
            for(const std::string_view& val: write.test_strings) serrf("\t%s\n", val.data());
            serr("result:\n");
            for(const std::string_view& val: read.test_strings) serrf("\t%s\n", val.data());
            return false;
        }
    }
    {   //read dynamic
        std::vector<bool> read_bools;
        std::vector<int> read_ints;
        std::vector<double> read_doubles;
        std::vector<std::string_view> read_strings;

        if(json.push_get_array_member("test_bools")){
            json.get_dynamic_array(read_bools);
            json.pop();
        }

        if(json.push_get_array_member("test_ints")){
            json.get_dynamic_array(read_ints);
            json.pop();
        }

        if(json.push_get_array_member("test_doubles")){
            json.get_dynamic_array(read_doubles);
            json.pop();
        }

        if(json.push_get_array_member("test_strings")){
            json.get_dynamic_array_string(read_strings);
            json.pop();
        }

        if(serr_check_error())
        {
            return false;
        }

        if(!std::equal(read_bools.begin(), read_bools.end(), 
                write.test_bools, write.test_bools + std::size(write.test_bools)))
        {
            serrf("test_bools inequal\n");
            serrf("original:\n");
            for(bool val: write.test_bools) serrf("\t%s\n", (val ? "true" : "false"));
            serr("result:\n");
            for(bool val: read_bools) serrf("\t%s\n", (val ? "true" : "false"));
            return false;
        }
        if(!std::equal(read_ints.begin(), read_ints.end(),
                write.test_ints, write.test_ints + std::size(write.test_ints)))
        {
            serrf("test_ints inequal\n");
            serrf("original:\n");
            for(int val: write.test_ints) serrf("\t%d\n", val);
            serr("result:\n");
            for(int val: read_ints) serrf("\t%d\n", val);
            return false;
        }
        if(!std::equal(read_doubles.begin(), read_doubles.end(), 
                write.test_doubles, write.test_doubles + std::size(write.test_doubles)))
        {
            serrf("test_doubles inequal\n");
            serrf("original:\n");
            for(double val: write.test_doubles) serrf("\t%f\n", val);
            serr("result:\n");
            for(double val: read_doubles) serrf("\t%f\n", val);
            return false;
        }
        if(!std::equal(read_strings.begin(), read_strings.end(), 
                write.test_strings, write.test_strings + std::size(write.test_strings)))
        {
            serrf("test_strings inequal\n");
            serrf("original:\n");
            for(const std::string_view& val: write.test_strings) serrf("\t%s\n", val.data());
            serr("result:\n");
            for(const std::string_view& val: read_strings) serrf("\t%s\n", val.data());
            return false;
        }
    }
    return true;
}

//test arrays
bool test_json_3()
{
    const array_stored_data write;
    //not a robust file, but just don't write a very large json file.
    //I could have a deque or stringstream based memory stream in RWops, but it's only useful for testing...
    char file_memory[9999];
    Unique_RWops mem_file(Unique_RWops_FromMemory(file_memory, sizeof(file_memory), false, __FUNCTION__));
    std::unique_ptr<char[]> true_memory;
    if(!mem_file)
    {
        return false;
    }
    {
        json_context json;
        json.create(mem_file->stream_info);

        json.push_set_array_member("test_bools");
        json.set_array(write.test_bools, write.test_bools + std::size(write.test_bools));
        json.pop();

        json.push_set_array_member("test_ints");
        json.set_array(write.test_ints, write.test_ints + std::size(write.test_ints));
        json.pop();

        json.push_set_array_member("test_doubles");
        json.set_array(write.test_doubles, write.test_doubles + std::size(write.test_doubles));
        json.pop();

        json.push_set_array_member("test_strings");
        json.set_array_string(write.test_strings, write.test_strings + std::size(write.test_strings));
        json.pop();

        if(serr_check_error())
        {
            return false;
        }

        if(!array_read_and_comp(json, write))
        {
            return false;
        }

        if(!json.write(mem_file.get()))
        {
            return false;
        }
        
        //shrink the file to fit (sometimes file->seek(0,SEEK_SET) works, but that is because the rest of the stack memory is filled with zeros.)
        int file_length = mem_file->tell();
        mem_file = Unique_RWops_FromMemory(file_memory, file_length, true, __FUNCTION__);
    }
    

    {
        json_context json;
        if(!json.open(mem_file.get()))
        {
            return false;
        }

        if(!array_read_and_comp(json, write))
        {
            return false;
        }
    }
    return true;
}

//test arrays by reading from a json file.
bool test_json_4()
{
    const array_stored_data write;
    std::ostringstream iss;
    {
        iss << "{\n";
        iss << "\t\"test_bools\": [";
        bool first_index = true;
        for(bool val: write.test_bools)
        {
            if(!first_index)
            {
                iss << ", ";
            }
            iss << (val ? "true" : "false");
            first_index = false;
        }

        iss << "],\n";
        iss << "\t\"test_ints\": [";
        first_index = true;
        for(int val: write.test_ints)
        {
            if(!first_index)
            {
                iss << ", ";
            }
            iss << val;
            first_index = false;
        }

        iss << "],\n";
        iss << "\t\"test_doubles\": [";
        first_index = true;
        for(double val: write.test_doubles)
        {
            if(!first_index)
            {
                iss << ", ";
            }
            iss << val;
            first_index = false;
        }

        iss << "],\n";
        iss << "\t\"test_strings\": [";
        first_index = true;
        for(const std::string_view& val: write.test_strings)
        {
            if(!first_index)
            {
                iss << ", ";
            }
            iss <<'\"'<< val<<'\"';
            first_index = false;
        }

        iss << "]\n";
        iss << "}";
    }
    std::string file_output(iss.str());
    Unique_RWops mem_file(Unique_RWops_FromMemory(file_output.data(), file_output.size(), true, __FUNCTION__));
    if(!mem_file)
    {
        return false;
    }

    json_context json;
    if(!json.open(mem_file.get()))
    {
        return false;
    }

    if(!array_read_and_comp(json, write))
    {
        return false;
    }
    return true;
}

//general example, no comparison checks due to laziness.
//this is also a mess (prints to slog), you can get rid of this, but it is better than nothing.
bool test_json_5()
{
    //not a robust file, but just don't write a very large json file.
    //I could have a deque or stringstream based memory stream in RWops, but it's only useful for testing...
    char file_memory[9999];
    Unique_RWops mem_file(Unique_RWops_FromMemory(file_memory, sizeof(file_memory), false, __FUNCTION__));
    {
        json_context json;
        json.create(mem_file->stream_info);

	    std::string test = "str1";
	    json.set_member_string("str1", test, JSON_COPY_VALUE);
        test.clear();
	    json.set_member_string("str2", std::string("str2"), JSON_COPY_VALUE);
	    json.set_member_string(std::string("str3"), "str3", JSON_COPY_KEY);
        json.set_member_string(std::string("str4"), std::string("str4"), JSON_COPY_KEY | JSON_COPY_VALUE);
	    json.set_member("value", true);

	    json.push_set_object_member("first_table");
		    json.push_set_array_member("array");
		    int array_buffer[] = {1,2,3};
		    json.set_array(array_buffer, array_buffer + std::size(array_buffer));
		    //equivilant to:
		    //for(int i: array_buffer) json.push_back(i);
		    json.pop(); //"array"
	    json.pop(); //"first_table"

	    json.push_set_array_member("2d_array");
		    int array_buffer_2d[][3] = {{1,2,3},{4,5,6},{7,8,9}, {10,11,12}};
		    for(size_t i = 0; i < std::size(array_buffer_2d); ++i)
		    {
			    json.push_set_array_index(i);
			    json.set_array(array_buffer_2d[i], array_buffer_2d[i] + std::size(array_buffer_2d[i]));
			    json.pop(); //i (index)
		    }
	    json.pop(); //"2d_array"

        if(serr_check_error())
        {
            return false;
        }

        if(!json.write(mem_file.get()))
        {
            return false;
        }

        //set the null terminator, and print
        //TODO: this will cause a OOB error in asan if the file is exactly the size of the buffer
        int pos = mem_file->tell();
        file_memory[pos] = '\0';
        slogf("[[[%s\n]]]", file_memory);
        
        //shrink the file to fit (sometimes file->seek(0,SEEK_SET) works, but that is because the rest of the stack memory is filled with zeros.)
        int file_length = mem_file->tell();
        mem_file = Unique_RWops_FromMemory(file_memory, file_length, true, __FUNCTION__);
    }
    {   //read
        json_context json;
        if(!json.open(mem_file.get()))
        {
            return false;
        }

        std::string_view get_string;
        json.get_member_string("str1", get_string);
        slogf("str1: %s\n", get_string.data());
	    json.get_member_string("str2", get_string);
        slogf("str2: %s\n", get_string.data());
	    json.get_member_string("str3", get_string);
        slogf("str3: %s\n", get_string.data());
        json.get_member_string("str4", get_string);
        slogf("str4: %s\n", get_string.data());

        bool get_bool;
        if(json.get_member("value", get_bool)) {
            slogf("value: %s\n", (get_bool ? "true" : "false"));
        }
        if(json.push_get_object_member("first_table"))
        {
            if(json.push_get_array_member("array"))
            {
                int array_buffer[3];
                if(json.get_array(array_buffer, array_buffer + std::size(array_buffer)))
                {
                    slogf("%s:\n", json.dump_path().c_str());
                    for(int val: array_buffer)
                    {
				        slogf("\t%d\n", val);
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
                        slogf("%s:\n", json.dump_path().c_str());
				        for(int val: array_buffer)
				        {
					        slogf("\t%d\n", val);
				        }
			        }
                    //instead of set_array for more dynamic usage:
#if 0
			        int size2 = json.top().GetArray().Size();
                    slogf("%s:\n", json.dump_path().c_str());
                    for(int j = 0; j < size2; ++j)
                    {
                        int get_number;
                        if(json.get_index(j, get_number)) {
					        slogf( "\t%d\n", get_number);
                        }
                    }
#endif
			        
                    json.pop(); //i (index)
                }
            }
            json.pop(); //"2d_array"
        }

        ASSERT(json.node_stack.empty() && "bad stack");

        if(serr_check_error())
        {
            return false;
        }
    }

    return true;
}
