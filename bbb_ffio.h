#pragma once
#ifndef BBB_FFIO
#define BBB_FFIO

#include <iostream>
#include <string>

#include <stdio.h>
#include <fileapi.h>

#include <ext/stdio_filebuf.h>

template<typename stream>
stream* open_wide_stream(std::wstring file, const wchar_t* parameter){
	FILE* c_file = _wfopen(file.c_str(), parameter);
	__gnu_cxx::stdio_filebuf<char>* buffer = new __gnu_cxx::stdio_filebuf<char>(c_file, std::ios_base::out | std::ios_base::binary , 100000);
	
	return new stream(buffer);
}

typedef struct byte_by_byte_fast_file_reader {
private:
	FILE* file;
	unsigned char* buffer;
	size_t buffer_size;
	size_t inner_buffer_pos;
	signed long long int file_pos;
	bool is_open;
	bool is_eof;
	bool next_chunk_is_unavailable;
	inline void __read_next_chunk() {
		if (!next_chunk_is_unavailable) {
			size_t new_buffer_len = fread(buffer, 1, buffer_size, file);
			if (new_buffer_len != buffer_size) {
				buffer_size = new_buffer_len;
				next_chunk_is_unavailable = true;
			}
			inner_buffer_pos = 0;
		}
		else {
			is_eof = true;
		}
	}
	inline unsigned char __get() {
		unsigned char buf_ch = buffer[inner_buffer_pos];
		inner_buffer_pos++;
		file_pos++;
		return buf_ch;
	}
public:
	byte_by_byte_fast_file_reader(const wchar_t* filename, int default_buffer_size = 10000000) {
		_set_errno(0);
		file = _wfopen(filename, L"rb");
		is_open = !(errno);
		next_chunk_is_unavailable = (is_eof = (file) ? feof(file) : true);
		if (!is_open | is_eof) {
			printf("\t\t%s\n",strerror(errno));
			file_pos = 0;
			buffer = nullptr;
			buffer_size = 0;
			inner_buffer_pos = 0;
		}
		else {
			file_pos = 0;
			inner_buffer_pos = 0;
			buffer = new unsigned char[default_buffer_size];
			buffer_size = default_buffer_size;
			ZeroMemory(buffer, default_buffer_size);
			__read_next_chunk();
		}
	}
	~byte_by_byte_fast_file_reader() {
		delete[] buffer;
		buffer = nullptr;
		if (is_open)
			fclose(file);
	}
	inline void reopen_next_file(const wchar_t* filename) {
		close(); 
		_set_errno(0);
		file = _wfopen(filename, L"rb");
		is_open = !(errno);
		next_chunk_is_unavailable = (is_eof = (file) ? feof(file) : true);
		if ((!is_open | is_eof) && buffer_size) {
			printf("\t\t%s\n",strerror(errno));
			file_pos = 0;
			buffer_size = 0;
			inner_buffer_pos = 0;
		}
		else {
			file_pos = 0;
			inner_buffer_pos = 0;
			ZeroMemory(buffer, buffer_size);
			__read_next_chunk();
		}
	}
	//rdbuf analogue
	inline void put_into_ostream(std::ostream& out) {
		while (!is_eof) {
			size_t offset = 0;
			file_pos += (offset = (buffer_size - inner_buffer_pos));
			out.write((char*)(buffer + inner_buffer_pos), offset);
			__read_next_chunk();
		}
		close();
	}
	inline void __seekg(signed long long int abs_pos) {
		_fseeki64(file, file_pos = abs_pos, 0); 
		__read_next_chunk();
	}
	inline unsigned char get() {
		if (is_open && !is_eof) {
			if (inner_buffer_pos >= buffer_size)
				__read_next_chunk();
			return is_eof ? 0 : __get();
		}
		else
			return 0;
	}
	inline void close() {
		if(is_open)
			fclose(file);
		is_eof = true;
		is_open = false;
	}
	inline signed long long int  tellg() const {
		return file_pos;
	}
	inline bool good() const {
		return is_open && !is_eof;
	}
	inline bool bad() const {
		return !good();
	}
	inline bool eof() const {
		return is_eof;
	}
	inline signed long long int tell_bufsize(){
		return buffer_size;
	}
} bbb_ffr;

#endif
