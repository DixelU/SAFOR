#include <stdio.h>
#include <fileapi.h>

typedef struct byte_by_byte_fast_file_reader {
private:
	FILE* file;
	unsigned char* buffer;
	size_t buffer_size;
	size_t inner_buffer_pos;
	INT64 file_pos;
	bool is_open;
	bool is_eof;
	bool next_chunk_is_unavailable;
	inline void __read_next_chunk() {
		if (!next_chunk_is_unavailable) {
			size_t new_buffer_len = fread(buffer,1,buffer_size,file);
			//printf("%lli\n", new_buffer_len);
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
	byte_by_byte_fast_file_reader(const char* filename, int default_buffer_size = 5000000) {
		file = fopen(filename, "rb");
		is_open = !feof(file);
		next_chunk_is_unavailable = is_eof = feof(file);
		if (!is_open) {
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
	inline void seekg(INT64 abs_pos) {
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
		if(file)
			fclose(file);
		is_eof = true;
		is_open = false;
	}
	inline INT64 tellg() const {
		return file_pos;
	}
	inline bool bad() {
		return !good();
	}
	inline bool good() {
		return is_open && !is_eof;
	}
	inline bool eof() {
		return is_eof;
	}
} bbb_ffr;

