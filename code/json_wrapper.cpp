#include "global.h"

#include "SDL_wrapper.h"
#include "json_wrapper.h"

#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>

const char* rj_string(const rj::Value& value)
{
    switch(value.GetType())
    {
    case rj::kNullType: return "null";
    case rj::kTrueType: return "true";
    case rj::kFalseType: return "false";
    case rj::kStringType: return "string";
    case rj::kObjectType: return "object";
    case rj::kArrayType: return "array";
    case rj::kNumberType:
        if(value.IsDouble()){
            return "double";
        } else if (value.IsInt()){
            return "int";
        } else {
            return "incompatible number";
        }
    }
    return "unknown";
}

//this is copy pasted from rapidjson::FileReadStream, but modified for use by SDL_RWops
//keeping the original comments because in the future I might check if there are changes.
 // Tencent is pleased to support the open source community by making RapidJSON available.
 // 
 // Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip.
 //
 // Licensed under the MIT License (the "License"); you may not use this file except
 // in compliance with the License. You may obtain a copy of the License at
 //
 // http://opensource.org/licenses/MIT
 //
 // Unless required by applicable law or agreed to in writing, software distributed 
 // under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR 
 // CONDITIONS OF ANY KIND, either express or implied. See the License for the 
 // specific language governing permissions and limitations under the License.
class RWops_JsonReadStream
{
public:
    typedef char Ch;    //!< Character type (byte).

    //! Constructor.
    /*!
        \param fp File pointer opened for read.
        \param buffer user-supplied buffer.
        \param bufferSize size of buffer in bytes. Must >=4 bytes.
    */
    RWops_JsonReadStream(RWops* fp, char* buffer, size_t bufferSize) : fp_(fp), buffer_(buffer), bufferSize_(bufferSize), bufferLast_(0), current_(buffer_), readCount_(0), count_(0), eof_(false) { 
        ASSERT(fp_ != 0);
        ASSERT(bufferSize >= 4);
        Read();
    }

    Ch Peek() const { return *current_; }
    Ch Take() { Ch c = *current_; Read(); return c; }
    size_t Tell() const { return count_ + static_cast<size_t>(current_ - buffer_); }

    // Not implemented
    void Put(Ch) { ASSERT(false); }
    void Flush() { ASSERT(false); } 
    Ch* PutBegin() { ASSERT(false); return 0; }
    size_t PutEnd(Ch*) { ASSERT(false); return 0; }

    // For encoding detection only.
    const Ch* Peek4() const {
        return (current_ + 4 - !eof_ <= bufferLast_) ? current_ : 0;
    }

private:
    void Read() {
        if (current_ < bufferLast_)
            ++current_;
        else if (!eof_) {
            count_ += readCount_;
            readCount_ = fp_->read(buffer_, 1, bufferSize_);
            bufferLast_ = buffer_ + readCount_ - 1;
            current_ = buffer_;

            if (readCount_ < bufferSize_) {
                buffer_[readCount_] = '\0';
                ++bufferLast_;
                eof_ = true;
            }
        }
    }

    RWops* fp_;
    Ch *buffer_;
    size_t bufferSize_;
    Ch *bufferLast_;
    Ch *current_;
    size_t readCount_;
    size_t count_;  //!< Number of characters read
    bool eof_;
};

//this is copy pasted from rapidjson::FileWriteStream

class RWops_JsonWriteStream {
public:
    typedef char Ch;    //!< Character type. Only support char.

    RWops_JsonWriteStream(RWops* fp, char* buffer, size_t bufferSize) : fp_(fp), buffer_(buffer), bufferEnd_(buffer + bufferSize), current_(buffer_) { 
        ASSERT(fp_ != 0);
    }

    void Put(char c) { 
        if (current_ >= bufferEnd_)
            Flush();

        *current_++ = c;
    }

    void PutN(char c, size_t n) {
        size_t avail = static_cast<size_t>(bufferEnd_ - current_);
        while (n > avail) {
            std::memset(current_, c, avail);
            current_ += avail;
            Flush();
            n -= avail;
            avail = static_cast<size_t>(bufferEnd_ - current_);
        }

        if (n > 0) {
            std::memset(current_, c, n);
            current_ += n;
        }
    }

    void Flush() {
        if (current_ != buffer_) {
            size_t result = fp_->write(buffer_, 1, static_cast<size_t>(current_ - buffer_));
            if (result < static_cast<size_t>(current_ - buffer_)) {
                // failure deliberately ignored at this time
                // added to avoid warn_unused_result build errors
            }
            current_ = buffer_;
        }
    }

    // Not implemented
    char Peek() const { ASSERT(false); return 0; }
    char Take() { ASSERT(false); return 0; }
    size_t Tell() const { ASSERT(false); return 0; }
    char* PutBegin() { ASSERT(false); return 0; }
    size_t PutEnd(char*) { ASSERT(false); return 0; }

private:
    // Prohibit copy constructor & assignment operator.
    RWops_JsonWriteStream(const RWops_JsonWriteStream&);
    RWops_JsonWriteStream& operator=(const RWops_JsonWriteStream&);

    RWops* fp_;
    char *buffer_;
    char *bufferEnd_;
    char *current_;
};

RAPIDJSON_NAMESPACE_BEGIN
//! Implement specialized version of PutN() with memset() for better performance.
template<>
inline void PutN(RWops_JsonWriteStream& stream, char c, size_t n) {
    stream.PutN(c, n);
}
RAPIDJSON_NAMESPACE_END

rj::Value* json_context::internal_find_member(const char* function, std::string_view key, int flags)
{
    ASSERT(top().IsObject());
    ASSERT(!key.empty());
    rj::Value::MemberIterator itr =
		top().FindMember(rj::StringRef(key.data(), key.size()));
	if(itr == top().MemberEnd())
	{
		if(!(flags & JSON_OPTIONAL))
		{
			serrf("json_context::%s Error in `%s`: No such node \"%s\"\n", function, file_info.c_str(), dump_path(key).c_str());
		}
		return NULL;
	}
	return &itr->value;
}
rj::Value* json_context::internal_find_index(const char* function, int index, int flags)
{
    ASSERT(top().IsArray());
    if(static_cast<int>(top().GetArray().Size()) <= index) {
        if(!(flags & JSON_OPTIONAL)){
            serrf("json_context::%s Error in `%s`: index out of bounds \"%s[%d]\" size=%d\n", 
            function,
            file_info.c_str(), 
            dump_path().c_str(), 
            index, 
            top().GetArray().Size());
        }
        return NULL;
    }
    return &top().GetArray()[index];
}

void json_context::internal_print_convert_error(const char* function, std::string_view key, rj::Value* value, const char* expected)
{
    serrf("json_context::%s Error in `%s`: Failed to convert key: \"%s\", value: %c%s%c, expected: [%s]\n", 
        function,
        file_info.c_str(),
        dump_path(key).c_str(),
        (value->IsString() ? '\"' : '['),
        (value->IsString() ? value->GetString() : rj_string(*value)),
        (value->IsString() ? '\"' : ']'),
        expected);
}

std::string json_context::dump_path(std::string_view key)
{
    std::string path;
    ASSERT((rj_doc.IsObject() || rj_doc.IsArray()) && "bad root");

    rj::Value* prev_node = &rj_doc;
    for(auto& node : node_stack)
    {
        ASSERT((node->data.IsObject() || node->data.IsArray()) && "bad node");
        if(prev_node->IsArray())
        {
            path += '[';
            path += std::to_string((node->index < 0) ? -(node->index + 1) : node->index);
            path += ']';
        } 
        else if(prev_node->IsObject())
        {
            //skip root
            if(prev_node != &rj_doc){
                path += '.';
            }
            path.append(node->key.GetString(), node->key.GetStringLength());
        }
        prev_node = &node->data;
    }
    if(!key.empty())
    {
        //skip root
        if(prev_node != &rj_doc){
            path += '.';
        }
        path += key;
    }
    return path;
}

bool json_context::open(RWops* file)
{
    ASSERT(file);

    file_info = file->stream_info;
    node_stack.clear();
    rj_doc.GetAllocator().Clear();
    context_flags = 0;

    char buffer[1024];
    RWops_JsonReadStream isw(file, buffer, sizeof(buffer));

	// rj::kParseCommentsFlag, rj::UTF8<> is to allow comments
	if(rj_doc.ParseStream<rj::kParseCommentsFlag, rj::UTF8<>>(isw).HasParseError())
	{
		int offset = rj_doc.GetErrorOffset();
		serrf("Failed to parse json: %s\n"
            "Message: %s\n"
            "Offset: %d\n", file_info.c_str(), GetParseError_En(rj_doc.GetParseError()), offset);

		// find the offending line:
		//std::string line;
		int line_n = 1; // should be 1 indexed
		if(file->seek(0, SEEK_SET) != 0)
        {
            serrf("SDL_RWseek: file stream can't seek\n");
            return false;
        }
		int prev_newline = 0;
        int buffers_passed = 0;
        int bytes_read;
        //note I am using the same buffer used for parsing the file
        //this could be 5x more simpler if I had getline...
		while((bytes_read = file->read(buffer, 1, sizeof(buffer))) > 0)
        {
            char* last = buffer + bytes_read;
            char* pos = buffer;
            do
            {
                pos = std::find(pos, last, '\n');
                int file_position = (buffers_passed * static_cast<int>(sizeof(buffer))) + (pos - buffer);
                ++pos;  //go over the newline for the next std::find
                if(offset <= file_position)
                {
                    serrf("Line: %d\n"
                            "Col: %d\n", line_n, (offset - prev_newline));
                    int line_length = file_position - prev_newline;
                    //note that I use (line_length + 1 >= sizeof(buffer)) to make sure there is an extra byte.
                    //for the line: *line_end = '\0';
                    if(line_length + 1 >= static_cast<int>(sizeof(buffer)))
                    {
                        serrf("line too long to print (len: %d)\n", line_length);
                        return false;
                    }
                    if(file->seek(prev_newline, RW_SEEK_SET) == -1)
                    {
                        serrf("SDL_RWseek: file stream can't seek\n");
                        return false;
                    }
                    if(static_cast<int>(file->read(buffer, 1, line_length)) != line_length)
                    {
                        serrf("SDL_RWread: could not read the line\n");
                        return false;
                    }
                    //clear any control values (mainly for windows \r\n)
                    char* line_end = std::remove_if(buffer, buffer+line_length,
                        [](auto _1){
                            return _1 != '\n' && _1 != '\t' && (static_cast<int>(_1) < 32 || static_cast<int>(_1) == 127 );
                        });
                    
                    *line_end = '\0';
                    //print the line.
                    serrf(">>>%s\n", buffer);
                    //I could print a tiny arrow to the column, but it doesn't work good with utf8.
                    return false;
                }
                ++line_n;
                prev_newline = file_position + 1; //+1 to skip newline, so when the error starts at line 1, prev_newline will be 0.
            } while(pos != last);
            ++buffers_passed;
        }
		return false;
	}
    return true;
}

bool json_context::write(RWops* file)
{
    ASSERT(file);
    ASSERT(node_stack.empty() && "invalid stack");

    file_info = file->stream_info;

    char buffer[1024];
	RWops_JsonWriteStream osw(file, buffer, sizeof(buffer));
	rj::PrettyWriter<RWops_JsonWriteStream> writer(osw);
	if(!rj_doc.Accept(writer))
	{
		serrf("Failed to write json: %s\n",file_info.c_str());
        const char* sdl_error = SDL_GetError();
        //I am not confident that the values printed here will always be correct.
        //maybe junk could fill it
        if(memcmp(sdl_error, "", 1) != 0)
        {
            serrf("possible SDL error: %s\n", sdl_error);
        }
        if(errno != 0)
        {
            serrf("possible errno: %s\n", strerror(errno));
        }
		return false;
	}
	return true;
}

void json_context::create(const char* info)
{
    ASSERT(info != NULL);
    file_info = info;
    node_stack.clear();
    rj_doc.GetAllocator().Clear();
    context_flags = 0;
    rj_doc.SetNull();
    rj_doc.SetObject();
}

void json_context::rename(const char* info)
{
    ASSERT(info != NULL);
    file_info = info;
}

void json_context::set_flags(int new_flags)
{
    context_flags = new_flags;
}

///
/// NODES -------------------------------
///

void json_context::pop()
{
    ASSERT(!node_stack.empty() && "stack underflow");

    //find the value before the last.
    rj::Value* found;
    if(node_stack.size() == 1)
    {
        found = &rj_doc;
    } else {
        found = &(*(node_stack.end()-2))->data;
    }
    ASSERT((found->IsObject() || found->IsArray()) && "bad pop");
    //pop inserts the rj::Value into the parent.
    if(found->IsObject()){
        rj::Value::MemberIterator itr = found->FindMember(rj::StringRef(node_stack.back()->key.GetString(), node_stack.back()->key.GetStringLength()));
        if(itr == found->MemberEnd())
        {
            found->AddMember(node_stack.back()->key, node_stack.back()->data, rj_doc.GetAllocator());
        } else {
            itr->value = node_stack.back()->data;
        }
    } else if(found->IsArray()) {
        int array_size = static_cast<int>(found->GetArray().Size());
        if(node_stack.back()->index < 0)
        {
            int index = -(node_stack.back()->index + 1);
            ASSERT(index < array_size && "out of bounds pop array getter");
            //if this was a node that was gotten, put it back.
            found->GetArray()[index] = node_stack.back()->data;
        } else {
            int index = node_stack.back()->index;
            ASSERT(array_size == index && "unaligned pop array setter");
            found->GetArray().PushBack(node_stack.back()->data, rj_doc.GetAllocator());
        }
    }
    node_stack.pop_back();
}

///
/// GET NODES -------------------------------
///

bool json_context::push_get_array_member(std::string_view key, int flags)
{
    rj::Value* it = internal_find_member(__FUNCTION__, key, flags);
    if(it == NULL) return false;
    if(!it->IsArray())
    {
        internal_print_convert_error(__FUNCTION__, key, it, "array");
        return false;
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, 
        (flags & JSON_COPY_KEY) 
        ? rj::Value(rj::StringRef(key.data(), key.size()), rj_doc.GetAllocator())
        : rj::Value(rj::StringRef(key.data(), key.size())), 0));
    return true;
}

bool json_context::push_get_array_index(int index, int flags)
{
    rj::Value* it = internal_find_index(__FUNCTION__, index, flags);
    if(it == NULL) return false;
    if(!it->IsArray())
    {
        internal_print_convert_error(__FUNCTION__, std::to_string(index), it, "array");
        return false;
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, rj::Value(), -(index+1)));
    return true;
}


bool json_context::push_get_object_member(std::string_view key, int flags)
{
    rj::Value* it = internal_find_member(__FUNCTION__, key, flags);
    if(it == NULL) return false;
    if(!it->IsObject())
    {
        internal_print_convert_error(__FUNCTION__, key, it, "object");
        return false;
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, 
        (flags & JSON_COPY_KEY) 
        ? rj::Value(rj::StringRef(key.data(), key.size()), rj_doc.GetAllocator())
        : rj::Value(rj::StringRef(key.data(), key.size())), 0));
    return true;
}

bool json_context::push_get_object_index(int index, int flags)
{
    rj::Value* it = internal_find_index(__FUNCTION__, index, flags);
    if(it == NULL) return false;
    if(!it->IsObject())
    {
        internal_print_convert_error(__FUNCTION__, std::to_string(index), it, "object");
        return false;
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, rj::Value(), -(index+1)));
    return true;
}

///
/// SET NODES -------------------------------
///

void json_context::push_set_array_member(std::string_view key, int flags)
{
    rj::Value* it = internal_find_member(__FUNCTION__, key, flags | JSON_OPTIONAL);
    rj::Value mem_holder;
    if(it == NULL)
    {
        mem_holder.SetArray();
        it = &mem_holder;
    } else {
        if(!it->IsArray())
        {
            it->SetArray();
        }
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, 
        (flags & JSON_COPY_KEY) 
        ? rj::Value(rj::StringRef(key.data(), key.size()), rj_doc.GetAllocator())
        : rj::Value(rj::StringRef(key.data(), key.size())), 0));
}

void json_context::push_set_array_index(int index, int flags)
{
    rj::Value* it = internal_find_index(__FUNCTION__, index, flags | JSON_OPTIONAL);
    rj::Value mem_holder;
    if(it == NULL)
    {
        mem_holder.SetArray();
        it = &mem_holder;
    } else {
        if(!it->IsArray())
        {
            it->SetArray();
        }
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, rj::Value(), index));
}

void json_context::push_set_object_member(std::string_view key, int flags)
{
    rj::Value* it = internal_find_member(__FUNCTION__, key, flags | JSON_OPTIONAL);
    rj::Value mem_holder;
    if(it == NULL)
    {
        mem_holder.SetObject();
        it = &mem_holder;
    } else {
        if(!it->IsObject())
        {
            it->SetObject();
        }
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, 
        (flags & JSON_COPY_KEY) 
        ? rj::Value(rj::StringRef(key.data(), key.size()), rj_doc.GetAllocator())
        : rj::Value(rj::StringRef(key.data(), key.size())), 0));
}

void json_context::push_set_object_index(int index, int flags)
{
    rj::Value* it = internal_find_index(__FUNCTION__, index, flags | JSON_OPTIONAL);
    rj::Value mem_holder;
    if(it == NULL)
    {
        mem_holder.SetObject();
        it = &mem_holder;
    } else {
        if(!it->IsObject())
        {
            it->SetObject();
        }
    }
    node_stack.emplace_back(std::make_unique<json_node>(*it, rj::Value(), index));
}



///
/// SETTERS -------------------------------
///

void json_context::set_member(std::string_view key, rj::Value value, int flags)
{
    rj::Value* it = internal_find_member(__FUNCTION__, key, flags | JSON_OPTIONAL);
    if(it != NULL)
    {
        
#if RAPIDJSON_MAJOR_VERSION == 1 && RAPIDJSON_MINOR_VERSION == 1
        if(value.IsString() && (flags & JSON_COPY_VALUE)){
            *it = rj::Value(rj::StringRef(value.GetString(), value.GetStringLength()), rj_doc.GetAllocator());
        } else {
            *it = (flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator()) : value;
        }
#else
        *it = (flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator(), true) : value;
#endif
    } else {
        
#if RAPIDJSON_MAJOR_VERSION == 1 && RAPIDJSON_MINOR_VERSION == 1
        rj::Value mem_holder;
        if(value.IsString() && (flags & JSON_COPY_VALUE)){
            mem_holder = rj::Value(rj::StringRef(value.GetString(), value.GetStringLength()), rj_doc.GetAllocator());
        } else {
            mem_holder = (flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator()) : value;
        }
#endif
        top().GetObject().AddMember((flags & JSON_COPY_KEY) 
            ? rj::Value(rj::StringRef(key.data(), key.size()), rj_doc.GetAllocator())
            : rj::Value(rj::StringRef(key.data(), key.size())),
#if RAPIDJSON_MAJOR_VERSION == 1 && RAPIDJSON_MINOR_VERSION == 1
            mem_holder, 
#else
            (flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator(), true) : value, 
#endif
            rj_doc.GetAllocator());
    }
}

void json_context::set_index(int index, rj::Value value, int flags)
{
    rj::Value* it = internal_find_index(__FUNCTION__, index, flags | JSON_OPTIONAL);
    if(it != NULL)
    {
#if RAPIDJSON_MAJOR_VERSION == 1 && RAPIDJSON_MINOR_VERSION == 1
        if(value.IsString() && (flags & JSON_COPY_VALUE)){
            *it = rj::Value(rj::StringRef(value.GetString(), value.GetStringLength()), rj_doc.GetAllocator());
        } else {
            *it = (flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator()) : value;
        }
#else
        //newer
        *it = (flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator(), true) : value;
#endif
    }
}
void json_context::push_back(rj::Value value, int flags)
{
    ASSERT(top().IsArray());
#if RAPIDJSON_MAJOR_VERSION == 1 && RAPIDJSON_MINOR_VERSION == 1
    if(value.IsString() && (flags & JSON_COPY_VALUE)){
        top().GetArray().PushBack(rj::Value(rj::StringRef(value.GetString(), value.GetStringLength()), rj_doc.GetAllocator()), rj_doc.GetAllocator());
    } else {
        top().GetArray().PushBack((flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator()) : value, rj_doc.GetAllocator());;
    }
#else
    //newer
    top().GetArray().PushBack((flags & JSON_COPY_VALUE) ? rj::Value().CopyFrom(value, rj_doc.GetAllocator(), true) : value, rj_doc.GetAllocator());
#endif
}
